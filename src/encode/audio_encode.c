#include "vmavificient/vmav_audio.h"
#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <sys/stat.h>

/* Encoder constants — match v1 exactly. */
#define VMAV_OPUS_SAMPLE_RATE 48000
#define VMAV_OPUS_PER_CHANNEL_BITRATE 56000

static bool is_french_lang(const char *lang) {
    return lang != NULL && (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0);
}

void vmav_audio_build_filename(char *buf,
                               size_t bufsize,
                               const char *base_name,
                               const char *language,
                               vmav_naming_french_variant_t fv) {
    if (buf == NULL || bufsize == 0 || base_name == NULL) {
        return;
    }
    if (is_french_lang(language)) {
        const char *suffix;
        switch (fv) {
        case VMAV_FR_VFQ:
            suffix = "fre.ca";
            break;
        case VMAV_FR_VFI:
            suffix = "fre.vfi";
            break;
        case VMAV_FR_VFF:
        case VMAV_FR_UNKNOWN:
        default:
            suffix = "fre.fr";
            break;
        }
        snprintf(buf, bufsize, "%s.%s.opus", base_name, suffix);
    } else {
        const char *lang = (language != NULL && language[0] != '\0') ? language : "und";
        snprintf(buf, bufsize, "%s.%s.opus", base_name, lang);
    }
}

/* Drain encoded packets from the encoder and write to output. */
static int drain_encoder(AVCodecContext *enc_ctx,
                         AVFormatContext *ofmt_ctx,
                         AVPacket *pkt,
                         AVStream *out_stream) {
    int rc;
    while ((rc = avcodec_receive_packet(enc_ctx, pkt)) == 0) {
        pkt->stream_index = out_stream->index;
        av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
        rc = av_interleaved_write_frame(ofmt_ctx, pkt);
        av_packet_unref(pkt);
        if (rc < 0) {
            return rc;
        }
    }
    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
        return 0;
    }
    return rc;
}

/* Feed samples from the FIFO to the encoder in frame_size chunks. */
static int encode_from_fifo(AVAudioFifo *fifo,
                            AVCodecContext *enc_ctx,
                            AVFormatContext *ofmt_ctx,
                            AVPacket *pkt,
                            AVStream *out_stream,
                            int64_t *next_pts,
                            bool flush) {
    const int frame_size = enc_ctx->frame_size;
    int rc;

    while (av_audio_fifo_size(fifo) >= frame_size || (flush && av_audio_fifo_size(fifo) > 0)) {
        int samples = av_audio_fifo_size(fifo);
        if (samples > frame_size) {
            samples = frame_size;
        }

        AVFrame *enc_frame = av_frame_alloc();
        if (enc_frame == NULL) {
            return AVERROR(ENOMEM);
        }
        enc_frame->nb_samples = samples;
        enc_frame->format = enc_ctx->sample_fmt;
        av_channel_layout_copy(&enc_frame->ch_layout, &enc_ctx->ch_layout);
        enc_frame->sample_rate = enc_ctx->sample_rate;

        rc = av_frame_get_buffer(enc_frame, 0);
        if (rc < 0) {
            av_frame_free(&enc_frame);
            return rc;
        }
        const int read = av_audio_fifo_read(fifo, (void **)enc_frame->data, samples);
        if (read < samples) {
            av_frame_free(&enc_frame);
            return AVERROR(EIO);
        }

        enc_frame->pts = *next_pts;
        *next_pts += read;

        rc = avcodec_send_frame(enc_ctx, enc_frame);
        av_frame_free(&enc_frame);
        if (rc < 0) {
            return rc;
        }
        rc = drain_encoder(enc_ctx, ofmt_ctx, pkt, out_stream);
        if (rc < 0) {
            return rc;
        }
    }
    return 0;
}

/* Push one (possibly empty) resampler output frame into the FIFO. */
static int swr_into_fifo(
    SwrContext *swr, AVFrame *resampled, AVFrame *in, AVCodecContext *enc_ctx, AVAudioFifo *fifo) {
    resampled->sample_rate = VMAV_OPUS_SAMPLE_RATE;
    resampled->format = AV_SAMPLE_FMT_FLT;
    av_channel_layout_copy(&resampled->ch_layout, &enc_ctx->ch_layout);

    int rc = swr_convert_frame(swr, resampled, in);
    if (rc < 0) {
        av_frame_unref(resampled);
        return rc;
    }
    if (resampled->nb_samples > 0) {
        rc = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + resampled->nb_samples);
        if (rc < 0) {
            av_frame_unref(resampled);
            return rc;
        }
        av_audio_fifo_write(fifo, (void **)resampled->data, resampled->nb_samples);
    }
    av_frame_unref(resampled);
    return resampled->nb_samples;
}

/* Verify the freshly-written file has an opus stream and is readable. */
static vmav_status_t verify_opus_file(const char *path) {
    AVFormatContext *fmt_ctx = NULL;
    int rc = avformat_open_input(&fmt_ctx, path, NULL, NULL);
    if (rc < 0) {
        return VMAV_ERR(VMAV_ERR_IO, "verify: cannot open '%s'", path);
    }
    rc = avformat_find_stream_info(fmt_ctx, NULL);
    if (rc < 0) {
        avformat_close_input(&fmt_ctx);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "verify: cannot probe '%s'", path);
    }
    bool found = false;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        const AVCodecParameters *p = fmt_ctx->streams[i]->codecpar;
        if (p->codec_type == AVMEDIA_TYPE_AUDIO && p->codec_id == AV_CODEC_ID_OPUS) {
            found = true;
            break;
        }
    }
    if (!found) {
        avformat_close_input(&fmt_ctx);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "verify: no OPUS stream in '%s'", path);
    }
    /* Read a handful of packets to confirm no truncation. */
    AVPacket *pkt = av_packet_alloc();
    int read = 0;
    while (pkt != NULL && read < 10 && av_read_frame(fmt_ctx, pkt) >= 0) {
        read++;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    if (read == 0) {
        return VMAV_ERR(VMAV_ERR_FFMPEG, "verify: no packets readable from '%s'", path);
    }
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_audio_encode_track(const char *input_path,
                                      const vmav_track_t *track,
                                      const char *output_path,
                                      vmav_audio_encode_t *out) {
    if (input_path == NULL || track == NULL || output_path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_audio_encode_track: null arg");
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->output_path, sizeof(out->output_path), "%s", output_path);

    struct stat st;
    if (stat(output_path, &st) == 0 && st.st_size > 0) {
        out->skipped = true;
        VMAV_LOG_INFO("audio_encode: '%s' already exists (%lld bytes), skipping",
                      output_path,
                      (long long)st.st_size);
        return VMAV_OK_STATUS;
    }

    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ofmt_ctx = NULL;
    AVCodecContext *dec_ctx = NULL;
    AVCodecContext *enc_ctx = NULL;
    SwrContext *swr = NULL;
    AVAudioFifo *fifo = NULL;
    AVPacket *pkt = NULL;
    AVPacket *out_pkt = NULL;
    AVFrame *frame = NULL;
    AVFrame *resampled = NULL;
    vmav_ui_progress_t *prog = NULL;
    vmav_status_t status = VMAV_OK_STATUS;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    int rc;

    rc = avformat_open_input(&ifmt_ctx, input_path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        status = VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", input_path, errbuf);
        goto cleanup;
    }
    rc = avformat_find_stream_info(ifmt_ctx, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "stream info: %s", errbuf);
        goto cleanup;
    }
    if (track->stream_index < 0 || (unsigned)track->stream_index >= ifmt_ctx->nb_streams) {
        status = VMAV_ERR(VMAV_ERR_BAD_ARG,
                          "stream index %d out of range (nb=%u)",
                          track->stream_index,
                          ifmt_ctx->nb_streams);
        goto cleanup;
    }
    AVStream *in_stream = ifmt_ctx->streams[track->stream_index];
    if (in_stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        status = VMAV_ERR(VMAV_ERR_BAD_ARG, "stream %d is not audio", track->stream_index);
        goto cleanup;
    }

    const AVCodec *decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
    if (decoder == NULL) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG,
                          "no decoder for codec %s",
                          avcodec_get_name(in_stream->codecpar->codec_id));
        goto cleanup;
    }
    dec_ctx = avcodec_alloc_context3(decoder);
    if (dec_ctx == NULL || avcodec_parameters_to_context(dec_ctx, in_stream->codecpar) < 0 ||
        avcodec_open2(dec_ctx, decoder, NULL) < 0) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "decoder open failed");
        goto cleanup;
    }

    const AVCodec *encoder = avcodec_find_encoder_by_name("libopus");
    if (encoder == NULL) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG,
                          "libopus encoder not registered in lavc — "
                          "build FFmpeg with --enable-libopus");
        goto cleanup;
    }
    enc_ctx = avcodec_alloc_context3(encoder);
    if (enc_ctx == NULL) {
        status = VMAV_ERR(VMAV_ERR_NO_MEM, "avcodec_alloc_context3 encoder");
        goto cleanup;
    }
    enc_ctx->bit_rate = (int64_t)track->channels * VMAV_OPUS_PER_CHANNEL_BITRATE;
    enc_ctx->sample_rate = VMAV_OPUS_SAMPLE_RATE;
    enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLT;
    enc_ctx->time_base = (AVRational){1, VMAV_OPUS_SAMPLE_RATE};
    av_channel_layout_default(&enc_ctx->ch_layout, dec_ctx->ch_layout.nb_channels);

    av_opt_set(enc_ctx->priv_data, "application", "audio", 0);
    av_opt_set(enc_ctx->priv_data, "vbr", "on", 0);
    av_opt_set_int(enc_ctx->priv_data, "compression_level", 10, 0);
    if (track->channels > 2) {
        av_opt_set_int(enc_ctx->priv_data, "mapping_family", 1, 0);
    }

    rc = avcodec_open2(enc_ctx, encoder, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "libopus encoder open: %s", errbuf);
        goto cleanup;
    }

    rc = swr_alloc_set_opts2(&swr,
                             &enc_ctx->ch_layout,
                             AV_SAMPLE_FMT_FLT,
                             VMAV_OPUS_SAMPLE_RATE,
                             &dec_ctx->ch_layout,
                             dec_ctx->sample_fmt,
                             dec_ctx->sample_rate,
                             0,
                             NULL);
    if (rc < 0 || swr == NULL || swr_init(swr) < 0) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "resampler setup failed");
        goto cleanup;
    }

    fifo =
        av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, enc_ctx->ch_layout.nb_channels, enc_ctx->frame_size);
    if (fifo == NULL) {
        status = VMAV_ERR(VMAV_ERR_NO_MEM, "av_audio_fifo_alloc");
        goto cleanup;
    }

    rc = avformat_alloc_output_context2(&ofmt_ctx, NULL, "opus", output_path);
    if (rc < 0 || ofmt_ctx == NULL) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "alloc output context");
        goto cleanup;
    }
    AVStream *out_stream = avformat_new_stream(ofmt_ctx, encoder);
    if (out_stream == NULL || avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) < 0) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "new output stream");
        goto cleanup;
    }
    out_stream->time_base = enc_ctx->time_base;

    if ((ofmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        rc = avio_open(&ofmt_ctx->pb, output_path, AVIO_FLAG_WRITE);
        if (rc < 0) {
            status = VMAV_ERR(VMAV_ERR_IO, "avio_open '%s'", output_path);
            goto cleanup;
        }
    }
    rc = avformat_write_header(ofmt_ctx, NULL);
    if (rc < 0) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "write header");
        goto cleanup;
    }

    pkt = av_packet_alloc();
    out_pkt = av_packet_alloc();
    frame = av_frame_alloc();
    resampled = av_frame_alloc();
    if (pkt == NULL || out_pkt == NULL || frame == NULL || resampled == NULL) {
        status = VMAV_ERR(VMAV_ERR_NO_MEM, "av_*_alloc");
        goto cleanup;
    }

    /* Total samples at output rate for the progress bar. */
    int64_t total_samples = 0;
    if (ifmt_ctx->duration > 0) {
        total_samples =
            (int64_t)((double)ifmt_ctx->duration / AV_TIME_BASE * VMAV_OPUS_SAMPLE_RATE);
    } else if (in_stream->duration > 0) {
        total_samples = av_rescale_q(
            in_stream->duration, in_stream->time_base, (AVRational){1, VMAV_OPUS_SAMPLE_RATE});
    }
    if (total_samples > 0) {
        prog = vmav_ui_progress_new(stderr, "audio-encode", (uint64_t)total_samples);
    }

    int64_t next_pts = 0;
    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != track->stream_index) {
            av_packet_unref(pkt);
            continue;
        }
        rc = avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            av_make_error_string(errbuf, sizeof(errbuf), rc);
            status = VMAV_ERR(VMAV_ERR_FFMPEG, "decode: %s", errbuf);
            goto cleanup;
        }
        while (avcodec_receive_frame(dec_ctx, frame) == 0) {
            const int pushed = swr_into_fifo(swr, resampled, frame, enc_ctx, fifo);
            av_frame_unref(frame);
            if (pushed < 0) {
                status = VMAV_ERR(VMAV_ERR_FFMPEG, "swr_convert_frame");
                goto cleanup;
            }
            rc = encode_from_fifo(fifo, enc_ctx, ofmt_ctx, out_pkt, out_stream, &next_pts, false);
            if (rc < 0) {
                status = VMAV_ERR(VMAV_ERR_FFMPEG, "encode_from_fifo");
                goto cleanup;
            }
            if (prog != NULL) {
                vmav_ui_progress_set(prog, (uint64_t)next_pts);
            }
        }
    }

    /* Drain decoder. */
    (void)avcodec_send_packet(dec_ctx, NULL);
    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
        const int pushed = swr_into_fifo(swr, resampled, frame, enc_ctx, fifo);
        av_frame_unref(frame);
        if (pushed < 0) {
            status = VMAV_ERR(VMAV_ERR_FFMPEG, "swr_convert_frame (drain)");
            goto cleanup;
        }
    }

    /* Drain resampler. */
    for (;;) {
        const int pushed = swr_into_fifo(swr, resampled, NULL, enc_ctx, fifo);
        if (pushed <= 0) {
            break;
        }
    }

    rc = encode_from_fifo(fifo, enc_ctx, ofmt_ctx, out_pkt, out_stream, &next_pts, true);
    if (rc < 0) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "final encode_from_fifo");
        goto cleanup;
    }
    (void)avcodec_send_frame(enc_ctx, NULL);
    rc = drain_encoder(enc_ctx, ofmt_ctx, out_pkt, out_stream);
    if (rc < 0) {
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "drain encoder");
        goto cleanup;
    }
    av_write_trailer(ofmt_ctx);
    if (prog != NULL && total_samples > 0) {
        vmav_ui_progress_finish(prog, NULL);
    }

    /* Close output before verifying so we don't read+write the same fd. */
    if (ofmt_ctx->pb != NULL && (ofmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        avio_closep(&ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);
    ofmt_ctx = NULL;

    status = verify_opus_file(output_path);

cleanup:
    if (prog != NULL) {
        vmav_ui_progress_free(prog);
    }
    av_frame_free(&resampled);
    av_frame_free(&frame);
    av_packet_free(&out_pkt);
    av_packet_free(&pkt);
    if (fifo != NULL) {
        av_audio_fifo_free(fifo);
    }
    swr_free(&swr);
    if (enc_ctx != NULL) {
        avcodec_free_context(&enc_ctx);
    }
    if (dec_ctx != NULL) {
        avcodec_free_context(&dec_ctx);
    }
    if (ofmt_ctx != NULL) {
        if (ofmt_ctx->pb != NULL && (ofmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&ofmt_ctx->pb);
        }
        avformat_free_context(ofmt_ctx);
    }
    if (ifmt_ctx != NULL) {
        avformat_close_input(&ifmt_ctx);
    }
    if (!vmav_status_ok(status) && !out->skipped) {
        remove(output_path);
    }
    return status;
}
