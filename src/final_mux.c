/**
 * @file final_mux.c
 * @brief Final MKV muxing: video + audio + subtitles with track names.
 *
 * Remuxes streams from separate files into a single MKV container,
 * setting track names, language tags, and default/forced dispositions.
 */

#include "final_mux.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>

/** @brief Context for a single input file being muxed. */
typedef struct {
  AVFormatContext *fmt_ctx;
  int stream_idx;     /**< Source stream index within this file. */
  int out_stream_idx; /**< Destination stream index in output. */
} InputSource;

/**
 * @brief Open an input file and find the first stream of the given type.
 */
static int open_input(InputSource *src, const char *path,
                      enum AVMediaType type) {
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  int ret;

  src->fmt_ctx = NULL;
  src->stream_idx = -1;
  src->out_stream_idx = -1;

  ret = avformat_open_input(&src->fmt_ctx, path, NULL, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "  Mux Error: cannot open '%s': %s\n", path, errbuf);
    return ret;
  }

  ret = avformat_find_stream_info(src->fmt_ctx, NULL);
  if (ret < 0) {
    avformat_close_input(&src->fmt_ctx);
    return ret;
  }

  for (unsigned i = 0; i < src->fmt_ctx->nb_streams; i++) {
    if (src->fmt_ctx->streams[i]->codecpar->codec_type == type) {
      src->stream_idx = (int)i;
      return 0;
    }
  }

  fprintf(stderr, "  Mux Error: no %s stream in '%s'\n",
          av_get_media_type_string(type), path);
  avformat_close_input(&src->fmt_ctx);
  return -1;
}

static void close_input(InputSource *src) {
  if (src->fmt_ctx)
    avformat_close_input(&src->fmt_ctx);
}

FinalMuxResult final_mux(const FinalMuxConfig *config) {
  FinalMuxResult result = {.error = 0, .skipped = 0};
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  int ret;

  /* Skip if output already exists */
  struct stat st;
  if (stat(config->output_path, &st) == 0 && st.st_size > 0) {
    result.skipped = 1;
    return result;
  }

  int total_inputs = 1 + config->audio_count + config->sub_count;
  InputSource *inputs = calloc(total_inputs, sizeof(InputSource));
  if (!inputs) {
    result.error = -1;
    return result;
  }

  AVFormatContext *ofmt_ctx = NULL;
  AVPacket *pkt = NULL;

  /* ---- Open video input ---- */
  ret = open_input(&inputs[0], config->video_path, AVMEDIA_TYPE_VIDEO);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  /* ---- Open audio inputs ---- */
  for (int i = 0; i < config->audio_count; i++) {
    ret = open_input(&inputs[1 + i], config->audio[i].path, AVMEDIA_TYPE_AUDIO);
    if (ret < 0) {
      result.error = ret;
      goto cleanup;
    }
  }

  /* ---- Open subtitle inputs ---- */
  for (int i = 0; i < config->sub_count; i++) {
    ret = open_input(&inputs[1 + config->audio_count + i],
                     config->subs[i].path, AVMEDIA_TYPE_SUBTITLE);
    if (ret < 0) {
      /* SRT files may not open with FFmpeg if they have issues — skip them */
      fprintf(stderr, "  Mux Warning: skipping subtitle '%s'\n",
              config->subs[i].path);
      inputs[1 + config->audio_count + i].fmt_ctx = NULL;
      continue;
    }
  }

  /* ---- Create output ---- */
  ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "matroska",
                                       config->output_path);
  if (ret < 0 || !ofmt_ctx) {
    fprintf(stderr, "  Mux Error: cannot create output context\n");
    result.error = -1;
    goto cleanup;
  }

  /* ---- Add video stream ---- */
  {
    InputSource *vs = &inputs[0];
    AVStream *in_s = vs->fmt_ctx->streams[vs->stream_idx];
    AVStream *out_s = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_s) {
      result.error = -1;
      goto cleanup;
    }
    avcodec_parameters_copy(out_s->codecpar, in_s->codecpar);
    out_s->time_base = in_s->time_base;
    out_s->codecpar->codec_tag = 0;

    /* Copy all coded side data (HDR metadata, DV config, etc.) */
    for (int sd = 0; sd < in_s->codecpar->nb_coded_side_data; sd++) {
      const AVPacketSideData *src_sd = &in_s->codecpar->coded_side_data[sd];
      AVPacketSideData *dst_sd = av_packet_side_data_new(
          &out_s->codecpar->coded_side_data,
          &out_s->codecpar->nb_coded_side_data, src_sd->type, src_sd->size, 0);
      if (dst_sd)
        memcpy(dst_sd->data, src_sd->data, src_sd->size);
    }

    vs->out_stream_idx = out_s->index;
  }

  /* ---- Add audio streams ---- */
  for (int i = 0; i < config->audio_count; i++) {
    InputSource *as = &inputs[1 + i];
    if (!as->fmt_ctx)
      continue;

    AVStream *in_s = as->fmt_ctx->streams[as->stream_idx];
    AVStream *out_s = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_s) {
      result.error = -1;
      goto cleanup;
    }
    avcodec_parameters_copy(out_s->codecpar, in_s->codecpar);
    out_s->time_base = in_s->time_base;
    out_s->codecpar->codec_tag = 0;

    /* Set track metadata */
    if (config->audio[i].track_name)
      av_dict_set(&out_s->metadata, "title", config->audio[i].track_name, 0);
    if (config->audio[i].language)
      av_dict_set(&out_s->metadata, "language", config->audio[i].language, 0);

    if (config->audio[i].is_default)
      out_s->disposition |= AV_DISPOSITION_DEFAULT;

    as->out_stream_idx = out_s->index;
  }

  /* ---- Add subtitle streams ---- */
  for (int i = 0; i < config->sub_count; i++) {
    InputSource *ss = &inputs[1 + config->audio_count + i];
    if (!ss->fmt_ctx)
      continue;

    AVStream *in_s = ss->fmt_ctx->streams[ss->stream_idx];
    AVStream *out_s = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_s) {
      result.error = -1;
      goto cleanup;
    }
    avcodec_parameters_copy(out_s->codecpar, in_s->codecpar);
    out_s->time_base = in_s->time_base;
    out_s->codecpar->codec_tag = 0;

    /* Set track metadata */
    if (config->subs[i].track_name)
      av_dict_set(&out_s->metadata, "title", config->subs[i].track_name, 0);
    if (config->subs[i].language)
      av_dict_set(&out_s->metadata, "language", config->subs[i].language, 0);

    if (config->subs[i].is_default)
      out_s->disposition |= AV_DISPOSITION_DEFAULT;
    if (config->subs[i].is_forced)
      out_s->disposition |= AV_DISPOSITION_FORCED;

    ss->out_stream_idx = out_s->index;
  }

  /* ---- Open output file ---- */
  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
      av_make_error_string(errbuf, sizeof(errbuf), ret);
      fprintf(stderr, "  Mux Error: cannot open '%s': %s\n",
              config->output_path, errbuf);
      result.error = ret;
      goto cleanup;
    }
  }

  /* ---- Set container title ---- */
  if (config->title)
    av_dict_set(&ofmt_ctx->metadata, "title", config->title, 0);

  /* ---- Write header ---- */
  ret = avformat_write_header(ofmt_ctx, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "  Mux Error: cannot write header: %s\n", errbuf);
    result.error = ret;
    goto cleanup;
  }

  /* ---- Copy packets from each input ---- */
  pkt = av_packet_alloc();
  if (!pkt) {
    result.error = -1;
    goto cleanup;
  }

  /* Copy all inputs in sequence (video first, then audio, then subs).
     For MKV muxing, FFmpeg will interleave correctly via
     av_interleaved_write_frame. */
  for (int src_i = 0; src_i < total_inputs; src_i++) {
    InputSource *src = &inputs[src_i];
    if (!src->fmt_ctx || src->out_stream_idx < 0)
      continue;

    AVStream *in_s = src->fmt_ctx->streams[src->stream_idx];
    AVStream *out_s = ofmt_ctx->streams[src->out_stream_idx];

    /* Seek to beginning of file */
    av_seek_frame(src->fmt_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);

    while (av_read_frame(src->fmt_ctx, pkt) >= 0) {
      if (pkt->stream_index != src->stream_idx) {
        av_packet_unref(pkt);
        continue;
      }

      pkt->stream_index = src->out_stream_idx;
      av_packet_rescale_ts(pkt, in_s->time_base, out_s->time_base);
      pkt->pos = -1;

      ret = av_interleaved_write_frame(ofmt_ctx, pkt);
      av_packet_unref(pkt);
      if (ret < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), ret);
        fprintf(stderr, "  Mux Warning: write error: %s\n", errbuf);
      }
    }
  }

  /* ---- Write trailer ---- */
  av_write_trailer(ofmt_ctx);

cleanup:
  av_packet_free(&pkt);

  for (int i = 0; i < total_inputs; i++)
    close_input(&inputs[i]);
  free(inputs);

  if (ofmt_ctx) {
    if (ofmt_ctx->pb && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
      avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
  }

  /* Remove output on failure */
  if (result.error != 0 && !result.skipped)
    remove(config->output_path);

  return result;
}
