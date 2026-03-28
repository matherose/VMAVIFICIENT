/**
 * @file media_analysis.c
 * @brief Video grain / noise analysis via signalstats TOUT and Y-Range.
 */

#include "media_analysis.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>

/** Number of sample points spread across the video. */
#define GRAIN_SAMPLES 6
/** Frames to decode per sample point. */
#define FRAMES_PER_SAMPLE 30

/**
 * @brief Build a buffersrc -> signalstats -> buffersink filter graph.
 */
static int build_signalstats_graph(AVFilterGraph **graph,
                                   AVFilterContext **src_ctx,
                                   AVFilterContext **sink_ctx,
                                   AVCodecContext *dec_ctx) {
  *graph = avfilter_graph_alloc();
  if (!*graph)
    return AVERROR(ENOMEM);

  const AVFilter *buffersrc = avfilter_get_by_name("buffer");
  const AVFilter *buffersink = avfilter_get_by_name("buffersink");
  const AVFilter *signalstats = avfilter_get_by_name("signalstats");

  char args[512];
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=1/25:pixel_aspect=1/1",
           dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt);

  int ret;
  ret = avfilter_graph_create_filter(src_ctx, buffersrc, "in", args, NULL,
                                     *graph);
  if (ret < 0)
    return ret;

  AVFilterContext *stats_ctx;
  ret = avfilter_graph_create_filter(&stats_ctx, signalstats, "signalstats",
                                     "stat=tout", NULL, *graph);
  if (ret < 0)
    return ret;

  ret = avfilter_graph_create_filter(sink_ctx, buffersink, "out", NULL, NULL,
                                     *graph);
  if (ret < 0)
    return ret;

  ret = avfilter_link(*src_ctx, 0, stats_ctx, 0);
  if (ret < 0)
    return ret;
  ret = avfilter_link(stats_ctx, 0, *sink_ctx, 0);
  if (ret < 0)
    return ret;

  return avfilter_graph_config(*graph, NULL);
}

GrainScore get_grain_score(const char *path) {
  GrainScore result = {0};
  AVFormatContext *fmt_ctx = NULL;
  AVCodecContext *dec_ctx = NULL;
  AVFilterGraph *graph = NULL;
  AVFilterContext *src_ctx = NULL, *sink_ctx = NULL;
  AVPacket *pkt = NULL;
  AVFrame *frame = NULL, *filt_frame = NULL;
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  int ret;

  ret = avformat_open_input(&fmt_ctx, path, NULL, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "Error: cannot open '%s': %s\n", path, errbuf);
    result.error = ret;
    return result;
  }

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "Error: cannot read stream info from '%s': %s\n", path,
            errbuf);
    result.error = ret;
    goto cleanup;
  }

  ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "Error: no video stream found in '%s': %s\n", path, errbuf);
    result.error = ret;
    goto cleanup;
  }

  int video_idx = ret;
  AVStream *stream = fmt_ctx->streams[video_idx];

  const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!decoder) {
    fprintf(stderr, "Error: unsupported video codec in '%s'.\n", path);
    result.error = AVERROR_DECODER_NOT_FOUND;
    goto cleanup;
  }

  dec_ctx = avcodec_alloc_context3(decoder);
  avcodec_parameters_to_context(dec_ctx, stream->codecpar);
  ret = avcodec_open2(dec_ctx, decoder, NULL);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  ret = build_signalstats_graph(&graph, &src_ctx, &sink_ctx, dec_ctx);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  pkt = av_packet_alloc();
  frame = av_frame_alloc();
  filt_frame = av_frame_alloc();
  if (!pkt || !frame || !filt_frame) {
    result.error = AVERROR(ENOMEM);
    goto cleanup;
  }

  int64_t duration = fmt_ctx->duration;
  if (duration <= 0)
    duration = 600 * AV_TIME_BASE;

  double total_tout = 0.0;
  double total_yrange = 0.0;
  int total_frames = 0;

  for (int s = 0; s < GRAIN_SAMPLES; s++) {
    int64_t target =
        duration / 10 + (int64_t)(duration * 0.8 / GRAIN_SAMPLES) * s;

    av_seek_frame(fmt_ctx, -1, target, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec_ctx);

    int frames_this_sample = 0;
    while (frames_this_sample < FRAMES_PER_SAMPLE &&
           av_read_frame(fmt_ctx, pkt) >= 0) {
      if (pkt->stream_index != video_idx) {
        av_packet_unref(pkt);
        continue;
      }
      avcodec_send_packet(dec_ctx, pkt);
      av_packet_unref(pkt);

      while (frames_this_sample < FRAMES_PER_SAMPLE &&
             avcodec_receive_frame(dec_ctx, frame) == 0) {
        av_buffersrc_add_frame(src_ctx, frame);
        av_frame_unref(frame);

        while (av_buffersink_get_frame(sink_ctx, filt_frame) == 0) {
          AVDictionaryEntry *e;

          e = av_dict_get(filt_frame->metadata, "lavfi.signalstats.TOUT", NULL,
                          0);
          if (e)
            total_tout += atof(e->value);

          double ylow = 0.0, yhigh = 0.0;
          e = av_dict_get(filt_frame->metadata, "lavfi.signalstats.YLOW", NULL,
                          0);
          if (e)
            ylow = atof(e->value);

          e = av_dict_get(filt_frame->metadata, "lavfi.signalstats.YHIGH", NULL,
                          0);
          if (e)
            yhigh = atof(e->value);

          total_yrange += (yhigh - ylow);
          frames_this_sample++;
          total_frames++;
          av_frame_unref(filt_frame);
        }
      }
    }
  }

  if (total_frames > 0) {
    result.avg_tout = total_tout / total_frames;
    result.avg_yrange = total_yrange / total_frames;
    result.frames_analyzed = total_frames;

    /* Normalise TOUT: typically 0-100 (percentage of outlier pixels). */
    double tout_norm = result.avg_tout / 100.0;
    if (tout_norm > 1.0)
      tout_norm = 1.0;

    /*
     * Normalise Y-Range: a narrow range (low contrast / flat image) can
     * indicate noise dominates the signal.  Full range = 255.
     * Invert so that narrow range → higher grain score.
     */
    double yrange_norm = 1.0 - (result.avg_yrange / 255.0);
    if (yrange_norm < 0.0)
      yrange_norm = 0.0;

    result.grain_score = 0.7 * tout_norm + 0.3 * yrange_norm;
  }

cleanup:
  av_packet_free(&pkt);
  av_frame_free(&frame);
  av_frame_free(&filt_frame);
  avfilter_graph_free(&graph);
  avcodec_free_context(&dec_ctx);
  avformat_close_input(&fmt_ctx);
  return result;
}
