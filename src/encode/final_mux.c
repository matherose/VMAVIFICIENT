/* Final MKV mux via libavformat (in-process). Replaces the v1-style
 * `ffmpeg` subprocess so vmavificient ships as a true single binary —
 * no runtime PATH dependency on the user having ffmpeg installed.
 *
 * Pipeline:
 *   1. Allocate output AVFormatContext for matroska.
 *   2. Open each input (1 video + N audio + M sub) as its own
 *      AVFormatContext; pick the best stream of the matching type.
 *   3. For each picked input stream, create a parallel output stream:
 *      avcodec_parameters_copy + preserve time_base + apply metadata
 *      (language, title) and disposition flags.
 *   4. Optionally copy chapters from `chapters_source`.
 *   5. Open the output file, write header (with matroska-specific
 *      `default_mode=infer_no_subs` so a subtitle isn't silently
 *      auto-marked default).
 *   6. Pump packets from each input, rewriting stream_index and
 *      rescaling timestamps; av_interleaved_write_frame handles the
 *      interleaving across streams.
 *   7. av_write_trailer; cleanup.
 *
 * Disposition mapping mirrors what v1's ffmpeg argv did:
 *   * Video: dispositions cleared (we own the stream now, so any
 *     inherited "default" or "attached_pic" flag shouldn't leak).
 *   * Audio: AV_DISPOSITION_DEFAULT iff is_default.
 *   * Subtitle: bitwise OR of DEFAULT/FORCED/HEARING_IMPAIRED per flags. */

#include "vmavificient/vmav_final_mux.h"
#include "vmavificient/vmav_log.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <sys/stat.h>

/* Per-input bookkeeping. */
typedef struct {
    AVFormatContext *fmt;
    int in_stream_index;  /* index in fmt->streams of the stream we keep */
    AVStream *out_stream; /* corresponding output stream in ofmt_ctx */
} mux_input_t;

static const char *fmt_av_err(int errnum, char *buf, size_t bufsize) {
    av_strerror(errnum, buf, bufsize);
    return buf;
}

static vmav_status_t open_input(const char *path,
                                enum AVMediaType media_type,
                                AVFormatContext **out_fmt,
                                int *out_stream_idx) {
    AVFormatContext *fmt = NULL;
    int rc = avformat_open_input(&fmt, path, NULL, NULL);
    if (rc < 0) {
        char errbuf[256];
        return VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", path, fmt_av_err(rc, errbuf, sizeof(errbuf)));
    }
    rc = avformat_find_stream_info(fmt, NULL);
    if (rc < 0) {
        avformat_close_input(&fmt);
        char errbuf[256];
        return VMAV_ERR(VMAV_ERR_FFMPEG,
                        "find_stream_info '%s': %s",
                        path,
                        fmt_av_err(rc, errbuf, sizeof(errbuf)));
    }
    const int stream_idx = av_find_best_stream(fmt, media_type, -1, -1, NULL, 0);
    if (stream_idx < 0) {
        avformat_close_input(&fmt);
        return VMAV_ERR(
            VMAV_ERR_NOT_FOUND, "no %s stream in '%s'", av_get_media_type_string(media_type), path);
    }
    *out_fmt = fmt;
    *out_stream_idx = stream_idx;
    return VMAV_OK_STATUS;
}

static vmav_status_t
add_output_stream(AVFormatContext *ofmt_ctx, AVStream *in_stream, AVStream **out_stream) {
    AVStream *out = avformat_new_stream(ofmt_ctx, NULL);
    if (out == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "avformat_new_stream");
    }
    const int rc = avcodec_parameters_copy(out->codecpar, in_stream->codecpar);
    if (rc < 0) {
        char errbuf[256];
        return VMAV_ERR(
            VMAV_ERR_FFMPEG, "avcodec_parameters_copy: %s", fmt_av_err(rc, errbuf, sizeof(errbuf)));
    }
    out->time_base = in_stream->time_base;
    /* Strip codec_tag — input formats often carry a tag that matroska
     * doesn't accept (e.g., a fourcc from IVF that maps to AV1 but
     * matroska wants no tag, just the codec_id). The muxer would
     * otherwise reject the stream at write_header. */
    out->codecpar->codec_tag = 0;
    *out_stream = out;
    return VMAV_OK_STATUS;
}

static vmav_status_t copy_chapters(AVFormatContext *src, AVFormatContext *dst) {
    if (src->nb_chapters == 0) {
        return VMAV_OK_STATUS;
    }
    AVChapter **new_chapters = av_realloc(dst->chapters, sizeof(*dst->chapters) * src->nb_chapters);
    if (new_chapters == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "realloc chapters");
    }
    dst->chapters = new_chapters;
    dst->nb_chapters = 0;
    for (unsigned i = 0; i < src->nb_chapters; i++) {
        const AVChapter *in_ch = src->chapters[i];
        AVChapter *out_ch = av_mallocz(sizeof(AVChapter));
        if (out_ch == NULL) {
            return VMAV_ERR(VMAV_ERR_NO_MEM, "mallocz chapter");
        }
        out_ch->id = in_ch->id;
        out_ch->time_base = in_ch->time_base;
        out_ch->start = in_ch->start;
        out_ch->end = in_ch->end;
        av_dict_copy(&out_ch->metadata, in_ch->metadata, 0);
        dst->chapters[i] = out_ch;
        dst->nb_chapters++;
    }
    return VMAV_OK_STATUS;
}

static vmav_status_t pump_one_input(AVFormatContext *in_fmt,
                                    int in_stream_idx,
                                    AVStream *out_stream,
                                    AVFormatContext *ofmt_ctx) {
    AVStream *in_stream = in_fmt->streams[in_stream_idx];
    AVPacket *pkt = av_packet_alloc();
    if (pkt == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "av_packet_alloc");
    }
    vmav_status_t status = VMAV_OK_STATUS;
    while (true) {
        int rc = av_read_frame(in_fmt, pkt);
        if (rc == AVERROR_EOF) {
            break;
        }
        if (rc < 0) {
            char errbuf[256];
            status = VMAV_ERR(
                VMAV_ERR_FFMPEG, "av_read_frame: %s", fmt_av_err(rc, errbuf, sizeof(errbuf)));
            break;
        }
        if (pkt->stream_index != in_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }
        pkt->stream_index = out_stream->index;
        av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
        pkt->pos = -1;
        rc = av_interleaved_write_frame(ofmt_ctx, pkt);
        av_packet_unref(pkt);
        if (rc < 0) {
            char errbuf[256];
            status = VMAV_ERR(VMAV_ERR_FFMPEG,
                              "av_interleaved_write_frame: %s",
                              fmt_av_err(rc, errbuf, sizeof(errbuf)));
            break;
        }
    }
    av_packet_free(&pkt);
    return status;
}

vmav_status_t vmav_final_mux(const vmav_final_mux_spec_t *spec, vmav_final_mux_result_t *out) {
    if (spec == NULL || out == NULL || spec->video_path == NULL || spec->output_path == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_final_mux: null arg");
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->output_path, sizeof(out->output_path), "%s", spec->output_path);

    /* Idempotent skip if the output already exists non-empty. */
    struct stat st;
    if (stat(spec->output_path, &st) == 0 && st.st_size > 0) {
        out->skipped = true;
        VMAV_LOG_INFO("final_mux: '%s' already exists (%lld bytes), skipping",
                      spec->output_path,
                      (long long)st.st_size);
        return VMAV_OK_STATUS;
    }

    vmav_status_t status = VMAV_OK_STATUS;
    AVFormatContext *ofmt_ctx = NULL;
    AVFormatContext *chapters_fmt = NULL;
    AVDictionary *muxer_opts = NULL;
    mux_input_t *inputs = NULL;
    const size_t input_count = 1 + spec->audio_count + spec->sub_count;
    bool header_written = false;

    inputs = calloc(input_count, sizeof(*inputs));
    if (inputs == NULL) {
        status = VMAV_ERR(VMAV_ERR_NO_MEM, "alloc inputs");
        goto cleanup;
    }

    int rc = avformat_alloc_output_context2(&ofmt_ctx, NULL, "matroska", spec->output_path);
    if (rc < 0 || ofmt_ctx == NULL) {
        char errbuf[256];
        status = VMAV_ERR(VMAV_ERR_FFMPEG,
                          "avformat_alloc_output_context2: %s",
                          fmt_av_err(rc, errbuf, sizeof(errbuf)));
        goto cleanup;
    }

    /* --- Video --- */
    status = open_input(
        spec->video_path, AVMEDIA_TYPE_VIDEO, &inputs[0].fmt, &inputs[0].in_stream_index);
    if (!vmav_status_ok(status)) {
        goto cleanup;
    }
    status = add_output_stream(
        ofmt_ctx, inputs[0].fmt->streams[inputs[0].in_stream_index], &inputs[0].out_stream);
    if (!vmav_status_ok(status)) {
        goto cleanup;
    }
    inputs[0].out_stream->disposition = 0; /* mirrors v1's `-disposition:v:0 0` */
    if (spec->video_title != NULL && spec->video_title[0] != '\0') {
        av_dict_set(&inputs[0].out_stream->metadata, "title", spec->video_title, 0);
    }
    if (spec->video_language != NULL && spec->video_language[0] != '\0') {
        av_dict_set(&inputs[0].out_stream->metadata, "language", spec->video_language, 0);
    }

    /* --- Audio tracks --- */
    for (size_t i = 0; i < spec->audio_count; i++) {
        const size_t idx = 1 + i;
        const vmav_mux_audio_t *au = &spec->audio[i];
        status = open_input(
            au->path, AVMEDIA_TYPE_AUDIO, &inputs[idx].fmt, &inputs[idx].in_stream_index);
        if (!vmav_status_ok(status)) {
            goto cleanup;
        }
        status = add_output_stream(ofmt_ctx,
                                   inputs[idx].fmt->streams[inputs[idx].in_stream_index],
                                   &inputs[idx].out_stream);
        if (!vmav_status_ok(status)) {
            goto cleanup;
        }
        const char *lang = (au->language != NULL && au->language[0] != '\0') ? au->language : "und";
        av_dict_set(&inputs[idx].out_stream->metadata, "language", lang, 0);
        if (au->track_name != NULL && au->track_name[0] != '\0') {
            av_dict_set(&inputs[idx].out_stream->metadata, "title", au->track_name, 0);
        }
        inputs[idx].out_stream->disposition = au->is_default ? AV_DISPOSITION_DEFAULT : 0;
    }

    /* --- Subtitle tracks --- */
    for (size_t i = 0; i < spec->sub_count; i++) {
        const size_t idx = 1 + spec->audio_count + i;
        const vmav_mux_sub_t *sb = &spec->subs[i];
        status = open_input(
            sb->path, AVMEDIA_TYPE_SUBTITLE, &inputs[idx].fmt, &inputs[idx].in_stream_index);
        if (!vmav_status_ok(status)) {
            goto cleanup;
        }
        status = add_output_stream(ofmt_ctx,
                                   inputs[idx].fmt->streams[inputs[idx].in_stream_index],
                                   &inputs[idx].out_stream);
        if (!vmav_status_ok(status)) {
            goto cleanup;
        }
        const char *lang = (sb->language != NULL && sb->language[0] != '\0') ? sb->language : "und";
        av_dict_set(&inputs[idx].out_stream->metadata, "language", lang, 0);
        if (sb->track_name != NULL && sb->track_name[0] != '\0') {
            av_dict_set(&inputs[idx].out_stream->metadata, "title", sb->track_name, 0);
        }
        int disp = 0;
        if (sb->is_default) {
            disp |= AV_DISPOSITION_DEFAULT;
        }
        if (sb->is_forced) {
            disp |= AV_DISPOSITION_FORCED;
        }
        if (sb->is_sdh) {
            disp |= AV_DISPOSITION_HEARING_IMPAIRED;
        }
        inputs[idx].out_stream->disposition = disp;
    }

    /* --- Chapters (best-effort) --- */
    if (spec->chapters_source != NULL && spec->chapters_source[0] != '\0') {
        rc = avformat_open_input(&chapters_fmt, spec->chapters_source, NULL, NULL);
        if (rc < 0) {
            VMAV_LOG_WARN("final_mux: open chapters_source '%s' failed (%d), skipping chapters",
                          spec->chapters_source,
                          rc);
        } else if (avformat_find_stream_info(chapters_fmt, NULL) >= 0) {
            status = copy_chapters(chapters_fmt, ofmt_ctx);
            if (!vmav_status_ok(status)) {
                goto cleanup;
            }
        }
    }

    if (spec->container_title != NULL && spec->container_title[0] != '\0') {
        av_dict_set(&ofmt_ctx->metadata, "title", spec->container_title, 0);
    }

    /* Matroska: don't auto-mark a subtitle as default when none is tagged. */
    av_dict_set(&muxer_opts, "default_mode", "infer_no_subs", 0);

    if ((ofmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        rc = avio_open(&ofmt_ctx->pb, spec->output_path, AVIO_FLAG_WRITE);
        if (rc < 0) {
            char errbuf[256];
            status = VMAV_ERR(VMAV_ERR_IO,
                              "avio_open '%s': %s",
                              spec->output_path,
                              fmt_av_err(rc, errbuf, sizeof(errbuf)));
            goto cleanup;
        }
    }

    rc = avformat_write_header(ofmt_ctx, &muxer_opts);
    if (rc < 0) {
        char errbuf[256];
        status = VMAV_ERR(
            VMAV_ERR_FFMPEG, "avformat_write_header: %s", fmt_av_err(rc, errbuf, sizeof(errbuf)));
        goto cleanup;
    }
    header_written = true;

    for (size_t i = 0; i < input_count; i++) {
        status = pump_one_input(
            inputs[i].fmt, inputs[i].in_stream_index, inputs[i].out_stream, ofmt_ctx);
        if (!vmav_status_ok(status)) {
            goto cleanup;
        }
    }

    rc = av_write_trailer(ofmt_ctx);
    if (rc < 0) {
        char errbuf[256];
        status = VMAV_ERR(
            VMAV_ERR_FFMPEG, "av_write_trailer: %s", fmt_av_err(rc, errbuf, sizeof(errbuf)));
        goto cleanup;
    }

cleanup:
    if (!vmav_status_ok(status) && header_written) {
        /* Best-effort: drop a partially-written output so a re-run
         * doesn't trip the "already exists, skipping" guard above. */
        remove(spec->output_path);
    }
    if (ofmt_ctx != NULL) {
        if (ofmt_ctx->pb != NULL && (ofmt_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
            avio_closep(&ofmt_ctx->pb);
        }
        avformat_free_context(ofmt_ctx);
    }
    if (chapters_fmt != NULL) {
        avformat_close_input(&chapters_fmt);
    }
    if (muxer_opts != NULL) {
        av_dict_free(&muxer_opts);
    }
    if (inputs != NULL) {
        for (size_t i = 0; i < input_count; i++) {
            if (inputs[i].fmt != NULL) {
                avformat_close_input(&inputs[i].fmt);
            }
        }
        free(inputs);
    }
    return status;
}
