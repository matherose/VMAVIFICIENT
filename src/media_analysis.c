/**
 * @file media_analysis.c
 * @brief Video grain / noise analysis via denoise-then-subtract method.
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
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>

/** Number of sample points spread across the video. */
#define GRAIN_SAMPLES 6
/** Frames to decode per sample point. */
#define FRAMES_PER_SAMPLE 30

/**
 * @brief Build a noise-estimation filter graph.
 *
 * Graph: buffer -> split -> hqdn3d(spatial) -> blend(difference) ->
 *        signalstats -> buffersink
 *
 * The difference between original and denoised frames isolates noise/grain.
 * signalstats YAVG on the difference measures the average noise level.
 */
static int build_noise_graph(AVFilterGraph **graph, AVFilterContext **src_ctx,
                             AVFilterContext **sink_ctx,
                             AVCodecContext *dec_ctx) {
  *graph = avfilter_graph_alloc();
  if (!*graph)
    return AVERROR(ENOMEM);

  const AVFilter *buffersrc = avfilter_get_by_name("buffer");
  const AVFilter *buffersink = avfilter_get_by_name("buffersink");

  char args[512];
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=1/25:pixel_aspect=1/1"
           ":colorspace=%d:range=%d",
           dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
           dec_ctx->colorspace, dec_ctx->color_range);

  int ret;
  ret = avfilter_graph_create_filter(src_ctx, buffersrc, "in", args, NULL,
                                     *graph);
  if (ret < 0)
    return ret;

  ret = avfilter_graph_create_filter(sink_ctx, buffersink, "out", NULL, NULL,
                                     *graph);
  if (ret < 0)
    return ret;

  AVFilterInOut *inputs = avfilter_inout_alloc();
  AVFilterInOut *outputs = avfilter_inout_alloc();
  if (!inputs || !outputs) {
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return AVERROR(ENOMEM);
  }

  outputs->name = av_strdup("in");
  outputs->filter_ctx = *src_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = av_strdup("out");
  inputs->filter_ctx = *sink_ctx;
  inputs->pad_idx = 0;
  inputs->next = NULL;

  ret = avfilter_graph_parse_ptr(
      *graph,
      "split[a][b];[b]hqdn3d=8:6:0:0[clean];"
      "[a][clean]blend=all_mode=difference,signalstats",
      &inputs, &outputs, NULL);
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);
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

  ret = build_noise_graph(&graph, &src_ctx, &sink_ctx, dec_ctx);
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

  double total_noise = 0.0;
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
        if (av_buffersrc_add_frame(src_ctx, frame) < 0) {
          av_frame_unref(frame);
          break;
        }
        av_frame_unref(frame);

        while (av_buffersink_get_frame(sink_ctx, filt_frame) == 0) {
          AVDictionaryEntry *e;

          e = av_dict_get(filt_frame->metadata, "lavfi.signalstats.YAVG", NULL,
                          0);
          if (e)
            total_noise += atof(e->value);

          frames_this_sample++;
          total_frames++;
          av_frame_unref(filt_frame);
        }
      }
    }
  }

  if (total_frames > 0) {
    result.avg_noise = total_noise / total_frames;
    result.frames_analyzed = total_frames;

    /*
     * Normalise: avg_noise is the mean pixel value of the difference
     * (original − denoised) frames.  Divide by the bit-depth maximum
     * and apply a scale factor so the score spans [0, 1] for typical
     * Blu-ray content.
     */
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(dec_ctx->pix_fmt);
    int bits = pix_desc ? pix_desc->comp[0].depth : 8;
    double max_val = (double)((1 << bits) - 1);

    result.grain_score = (result.avg_noise / max_val) * 20.0;
    if (result.grain_score > 1.0)
      result.grain_score = 1.0;
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
