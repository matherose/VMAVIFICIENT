/**
 * @file audio_encode.c
 * @brief Implementation of audio track encoding to OPUS format.
 */

#include "audio_encode.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

/** @brief Check if a 3-letter language code is French. */
static int is_french(const char *lang) {
  return strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0;
}

void build_opus_filename(char *buf, size_t bufsize, const char *base_name,
                         const char *language, FrenchVariant fv) {
  if (is_french(language)) {
    const char *suffix;
    switch (fv) {
    case FRENCH_VARIANT_VFQ:
      suffix = "fre.ca";
      break;
    case FRENCH_VARIANT_VFI:
      suffix = "fre.vfi";
      break;
    default:
      suffix = "fre.fr";
      break;
    }
    snprintf(buf, bufsize, "%s.%s.opus", base_name, suffix);
  } else {
    const char *lang = (language && language[0]) ? language : "und";
    snprintf(buf, bufsize, "%s.%s.opus", base_name, lang);
  }
}

int verify_opus_file(const char *path) {
  AVFormatContext *fmt_ctx = NULL;
  int ret = avformat_open_input(&fmt_ctx, path, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "  Verify: cannot open '%s'\n", path);
    return ret;
  }

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "  Verify: cannot read stream info from '%s'\n", path);
    avformat_close_input(&fmt_ctx);
    return ret;
  }

  /* Check that at least one audio stream with OPUS codec exists. */
  int found_opus = 0;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
        fmt_ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_OPUS) {
      found_opus = 1;
      break;
    }
  }

  if (!found_opus) {
    fprintf(stderr, "  Verify: no OPUS audio stream in '%s'\n", path);
    avformat_close_input(&fmt_ctx);
    return -1;
  }

  /* Read a few packets to confirm the file is not truncated. */
  AVPacket *pkt = av_packet_alloc();
  int packets_read = 0;
  while (packets_read < 10 && av_read_frame(fmt_ctx, pkt) >= 0) {
    packets_read++;
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);

  avformat_close_input(&fmt_ctx);

  if (packets_read == 0) {
    fprintf(stderr, "  Verify: no packets readable from '%s'\n", path);
    return -1;
  }

  return 0;
}

/**
 * @brief Drain encoded packets from the encoder and write to output.
 */
static int drain_encoder(AVCodecContext *enc_ctx, AVFormatContext *ofmt_ctx,
                         AVPacket *pkt, AVStream *out_stream) {
  int ret;
  while ((ret = avcodec_receive_packet(enc_ctx, pkt)) == 0) {
    pkt->stream_index = out_stream->index;
    av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    av_packet_unref(pkt);
    if (ret < 0)
      return ret;
  }
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    return 0;
  return ret;
}

/**
 * @brief Feed samples from the FIFO to the encoder in frame_size chunks.
 */
static int encode_from_fifo(AVAudioFifo *fifo, AVCodecContext *enc_ctx,
                            AVFormatContext *ofmt_ctx, AVPacket *pkt,
                            AVStream *out_stream, int64_t *next_pts,
                            int flush) {
  int frame_size = enc_ctx->frame_size;
  int ret;

  while (av_audio_fifo_size(fifo) >= frame_size ||
         (flush && av_audio_fifo_size(fifo) > 0)) {
    int samples_to_read = av_audio_fifo_size(fifo);
    if (samples_to_read > frame_size)
      samples_to_read = frame_size;

    AVFrame *enc_frame = av_frame_alloc();
    if (!enc_frame)
      return AVERROR(ENOMEM);

    enc_frame->nb_samples = samples_to_read;
    enc_frame->format = enc_ctx->sample_fmt;
    av_channel_layout_copy(&enc_frame->ch_layout, &enc_ctx->ch_layout);
    enc_frame->sample_rate = enc_ctx->sample_rate;

    ret = av_frame_get_buffer(enc_frame, 0);
    if (ret < 0) {
      av_frame_free(&enc_frame);
      return ret;
    }

    int read =
        av_audio_fifo_read(fifo, (void **)enc_frame->data, samples_to_read);
    if (read < samples_to_read) {
      av_frame_free(&enc_frame);
      return AVERROR(EIO);
    }

    enc_frame->pts = *next_pts;
    *next_pts += read;

    ret = avcodec_send_frame(enc_ctx, enc_frame);
    av_frame_free(&enc_frame);
    if (ret < 0)
      return ret;

    ret = drain_encoder(enc_ctx, ofmt_ctx, pkt, out_stream);
    if (ret < 0)
      return ret;
  }

  return 0;
}

/**
 * @brief Print a progress bar with speed and ETA to stderr.
 *
 * Format: "  [=========>          ] 45%  12.5x  ETA 02:15"
 * Speed is a realtime multiplier (seconds of audio per wall-clock second).
 */
static void print_progress(int64_t current_pts, int64_t total_samples,
                           time_t start_time) {
  if (total_samples <= 0)
    return;

  double pct = (double)current_pts / total_samples;
  if (pct > 1.0)
    pct = 1.0;

  int bar_width = 30;
  int filled = (int)(pct * bar_width);

  char bar[64];
  for (int i = 0; i < bar_width; i++) {
    if (i < filled)
      bar[i] = '=';
    else if (i == filled)
      bar[i] = '>';
    else
      bar[i] = ' ';
  }
  bar[bar_width] = '\0';

  time_t now = time(NULL);
  double elapsed = difftime(now, start_time);

  /* Speed: audio seconds encoded per wall-clock second. */
  char speed_str[32] = "";
  if (elapsed > 1.0) {
    double audio_secs = (double)current_pts / 48000.0;
    double speed = audio_secs / elapsed;
    snprintf(speed_str, sizeof(speed_str), "%.1fx", speed);
  }

  /* ETA. */
  char eta_str[32] = "";
  if (pct > 0.01 && elapsed > 1.0) {
    double remaining = elapsed * (1.0 - pct) / pct;
    int eta_min = (int)(remaining / 60);
    int eta_sec = (int)remaining % 60;
    snprintf(eta_str, sizeof(eta_str), "ETA %02d:%02d", eta_min, eta_sec);
  }

  fprintf(stderr, "\r  [%s] %3d%%  %6s  %s   ", bar, (int)(pct * 100),
          speed_str, eta_str);
  fflush(stderr);
}

OpusEncodeResult encode_track_to_opus(const char *input_path,
                                      const TrackInfo *track,
                                      const char *output_path) {
  OpusEncodeResult result = {.error = 0, .skipped = 0};
  snprintf(result.output_path, sizeof(result.output_path), "%s", output_path);

  /* Skip if output already exists. */
  struct stat st;
  if (stat(output_path, &st) == 0 && st.st_size > 0) {
    result.skipped = 1;
    return result;
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
  int ret;

  /* ── Open input ── */
  ret = avformat_open_input(&ifmt_ctx, input_path, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "  Error: cannot open input '%s'\n", input_path);
    result.error = ret;
    goto cleanup;
  }

  ret = avformat_find_stream_info(ifmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "  Error: cannot probe input streams\n");
    result.error = ret;
    goto cleanup;
  }

  /* Find the target audio stream. */
  if (track->index < 0 || (unsigned)track->index >= ifmt_ctx->nb_streams) {
    fprintf(stderr, "  Error: stream index %d out of range\n", track->index);
    result.error = -1;
    goto cleanup;
  }

  AVStream *in_stream = ifmt_ctx->streams[track->index];
  if (in_stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
    fprintf(stderr, "  Error: stream %d is not audio\n", track->index);
    result.error = -1;
    goto cleanup;
  }

  /* ── Set up decoder ── */
  const AVCodec *decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
  if (!decoder) {
    fprintf(stderr, "  Error: no decoder for codec %s\n",
            avcodec_get_name(in_stream->codecpar->codec_id));
    result.error = -1;
    goto cleanup;
  }

  dec_ctx = avcodec_alloc_context3(decoder);
  if (!dec_ctx) {
    result.error = AVERROR(ENOMEM);
    goto cleanup;
  }

  ret = avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  ret = avcodec_open2(dec_ctx, decoder, NULL);
  if (ret < 0) {
    fprintf(stderr, "  Error: cannot open decoder\n");
    result.error = ret;
    goto cleanup;
  }

  /* ── Set up OPUS encoder ── */
  const AVCodec *encoder = avcodec_find_encoder_by_name("libopus");
  if (!encoder) {
    fprintf(stderr, "  Error: libopus encoder not found\n");
    result.error = -1;
    goto cleanup;
  }

  enc_ctx = avcodec_alloc_context3(encoder);
  if (!enc_ctx) {
    result.error = AVERROR(ENOMEM);
    goto cleanup;
  }

  int audio_bitrate = track->channels * 56000;
  enc_ctx->bit_rate = audio_bitrate;
  enc_ctx->sample_rate = 48000;
  enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLT;
  enc_ctx->time_base = (AVRational){1, 48000};

  /* libopus only supports standard channel layouts. Map the decoder's
     layout to a default one with the same channel count so that
     variants like 5.1(side) are accepted. */
  av_channel_layout_default(&enc_ctx->ch_layout,
                            dec_ctx->ch_layout.nb_channels);

  av_opt_set(enc_ctx->priv_data, "application", "audio", 0);
  av_opt_set(enc_ctx->priv_data, "vbr", "on", 0);
  av_opt_set_int(enc_ctx->priv_data, "compression_level", 10, 0);

  /* Multichannel: mapping_family=1 for correct 5.1/7.1/7.1+ layout */
  if (track->channels > 2)
    av_opt_set_int(enc_ctx->priv_data, "mapping_family", 1, 0);

  ret = avcodec_open2(enc_ctx, encoder, NULL);
  if (ret < 0) {
    fprintf(stderr, "  Error: cannot open OPUS encoder (error %d)\n", ret);
    result.error = ret;
    goto cleanup;
  }

  /* ── Set up resampler ── */
  ret = swr_alloc_set_opts2(&swr, &enc_ctx->ch_layout, AV_SAMPLE_FMT_FLT, 48000,
                            &dec_ctx->ch_layout, dec_ctx->sample_fmt,
                            dec_ctx->sample_rate, 0, NULL);
  if (ret < 0 || !swr) {
    fprintf(stderr, "  Error: cannot allocate resampler\n");
    result.error = ret < 0 ? ret : AVERROR(ENOMEM);
    goto cleanup;
  }

  ret = swr_init(swr);
  if (ret < 0) {
    fprintf(stderr, "  Error: cannot init resampler\n");
    result.error = ret;
    goto cleanup;
  }

  /* ── Audio FIFO ── */
  fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, enc_ctx->ch_layout.nb_channels,
                             enc_ctx->frame_size);
  if (!fifo) {
    result.error = AVERROR(ENOMEM);
    goto cleanup;
  }

  /* ── Open output muxer ── */
  ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "opus", output_path);
  if (ret < 0 || !ofmt_ctx) {
    fprintf(stderr, "  Error: cannot create output context\n");
    result.error = ret < 0 ? ret : AVERROR(ENOMEM);
    goto cleanup;
  }

  AVStream *out_stream = avformat_new_stream(ofmt_ctx, encoder);
  if (!out_stream) {
    result.error = AVERROR(ENOMEM);
    goto cleanup;
  }

  ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }
  out_stream->time_base = enc_ctx->time_base;

  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, output_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
      fprintf(stderr, "  Error: cannot open output file '%s'\n", output_path);
      result.error = ret;
      goto cleanup;
    }
  }

  ret = avformat_write_header(ofmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "  Error: cannot write output header\n");
    result.error = ret;
    goto cleanup;
  }

  /* ── Decode / resample / encode loop ── */
  pkt = av_packet_alloc();
  out_pkt = av_packet_alloc();
  frame = av_frame_alloc();
  resampled = av_frame_alloc();
  if (!pkt || !out_pkt || !frame || !resampled) {
    result.error = AVERROR(ENOMEM);
    goto cleanup;
  }

  /* Total samples at output rate for progress tracking. */
  int64_t total_samples = 0;
  if (ifmt_ctx->duration > 0)
    total_samples =
        (int64_t)((double)ifmt_ctx->duration / AV_TIME_BASE * 48000);
  else if (in_stream->duration > 0)
    total_samples = av_rescale_q(in_stream->duration, in_stream->time_base,
                                 (AVRational){1, 48000});

  time_t start_time = time(NULL);
  time_t last_progress = 0;
  int64_t next_pts = 0;

  while (av_read_frame(ifmt_ctx, pkt) >= 0) {
    if (pkt->stream_index != track->index) {
      av_packet_unref(pkt);
      continue;
    }

    ret = avcodec_send_packet(dec_ctx, pkt);
    av_packet_unref(pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
      result.error = ret;
      goto cleanup;
    }

    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
      /* Resample to 48kHz float. */
      resampled->sample_rate = 48000;
      resampled->format = AV_SAMPLE_FMT_FLT;
      av_channel_layout_copy(&resampled->ch_layout, &enc_ctx->ch_layout);

      ret = swr_convert_frame(swr, resampled, frame);
      av_frame_unref(frame);
      if (ret < 0) {
        fprintf(stderr, "  Error: resample failed\n");
        result.error = ret;
        goto cleanup;
      }

      /* Push resampled data into FIFO. */
      ret = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) +
                                            resampled->nb_samples);
      if (ret < 0) {
        av_frame_unref(resampled);
        result.error = ret;
        goto cleanup;
      }

      av_audio_fifo_write(fifo, (void **)resampled->data,
                          resampled->nb_samples);
      av_frame_unref(resampled);

      /* Encode full frames from FIFO. */
      ret = encode_from_fifo(fifo, enc_ctx, ofmt_ctx, out_pkt, out_stream,
                             &next_pts, 0);
      if (ret < 0) {
        result.error = ret;
        goto cleanup;
      }

      /* Update progress at most once per second. */
      time_t now = time(NULL);
      if (now != last_progress) {
        print_progress(next_pts, total_samples, start_time);
        last_progress = now;
      }
    }
  }

  /* ── Flush decoder ── */
  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, frame) == 0) {
    resampled->sample_rate = 48000;
    resampled->format = AV_SAMPLE_FMT_FLT;
    av_channel_layout_copy(&resampled->ch_layout, &enc_ctx->ch_layout);

    ret = swr_convert_frame(swr, resampled, frame);
    av_frame_unref(frame);
    if (ret >= 0) {
      if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) +
                                          resampled->nb_samples) >= 0) {
        av_audio_fifo_write(fifo, (void **)resampled->data,
                            resampled->nb_samples);
      }
      av_frame_unref(resampled);
    }
  }

  /* ── Flush resampler ── */
  for (;;) {
    resampled->sample_rate = 48000;
    resampled->format = AV_SAMPLE_FMT_FLT;
    av_channel_layout_copy(&resampled->ch_layout, &enc_ctx->ch_layout);

    ret = swr_convert_frame(swr, resampled, NULL);
    if (ret < 0 || resampled->nb_samples == 0) {
      av_frame_unref(resampled);
      break;
    }
    if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) +
                                        resampled->nb_samples) >= 0) {
      av_audio_fifo_write(fifo, (void **)resampled->data,
                          resampled->nb_samples);
    }
    av_frame_unref(resampled);
  }

  /* ── Flush FIFO remainder ── */
  ret = encode_from_fifo(fifo, enc_ctx, ofmt_ctx, out_pkt, out_stream,
                         &next_pts, 1);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  /* ── Flush encoder ── */
  avcodec_send_frame(enc_ctx, NULL);
  ret = drain_encoder(enc_ctx, ofmt_ctx, out_pkt, out_stream);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  av_write_trailer(ofmt_ctx);

  /* Final progress: 100%. */
  print_progress(total_samples, total_samples, start_time);
  time_t end_time = time(NULL);
  int elapsed = (int)difftime(end_time, start_time);
  fprintf(stderr, "\r  [");
  for (int i = 0; i < 30; i++)
    fprintf(stderr, "=");
  fprintf(stderr, "] 100%%  Done in %02d:%02d          \n", elapsed / 60,
          elapsed % 60);

  /* ── Verify output ── */
  ret = verify_opus_file(output_path);
  if (ret < 0) {
    fprintf(stderr, "  Warning: output verification failed for '%s'\n",
            output_path);
    result.error = ret;
  }

cleanup:
  av_frame_free(&resampled);
  av_frame_free(&frame);
  av_packet_free(&out_pkt);
  av_packet_free(&pkt);
  if (fifo)
    av_audio_fifo_free(fifo);
  swr_free(&swr);
  if (enc_ctx)
    avcodec_free_context(&enc_ctx);
  if (dec_ctx)
    avcodec_free_context(&dec_ctx);
  if (ofmt_ctx) {
    if (ofmt_ctx->pb && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
      avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
  }
  if (ifmt_ctx)
    avformat_close_input(&ifmt_ctx);

  /* Remove output on failure so partial files don't get reused. */
  if (result.error != 0 && !result.skipped)
    remove(output_path);

  return result;
}
