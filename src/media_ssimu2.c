/**
 * @file media_ssimu2.c
 * @brief libav adapter for the embedded SSIMULACRA2 metric.
 *
 * Decodes two video files in lockstep, normalises each frame to
 * YUV420P10LE, and calls the `vmav_ssimu2_score_yuv420p10` FFI once per
 * frame pair. Per-frame scores are aggregated into mean / median / p10.
 */

#include "media_ssimu2.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

/* ====================================================================== */
/*  Internal decoder helper                                               */
/* ====================================================================== */

/**
 * @brief Bundled libav state for decoding one video file frame-by-frame
 *        and handing back YUV420P10LE frames to the caller.
 */
typedef struct {
  AVFormatContext *fmt_ctx;
  AVCodecContext *dec_ctx;
  struct SwsContext *sws;   /**< NULL if source is already yuv420p10le. */
  AVFrame *raw;             /**< Frame in the decoder's native pix_fmt. */
  AVFrame *yuv420p10;       /**< Frame after sws conversion (or alias of raw). */
  AVPacket *pkt;
  int video_idx;
} Decoder;

/**
 * @brief Tear down all resources held by a Decoder.
 */
static void decoder_close(Decoder *d) {
  if (!d)
    return;
  if (d->sws) {
    sws_freeContext(d->sws);
    d->sws = NULL;
  }
  av_frame_free(&d->raw);
  av_frame_free(&d->yuv420p10);
  av_packet_free(&d->pkt);
  avcodec_free_context(&d->dec_ctx);
  avformat_close_input(&d->fmt_ctx);
  memset(d, 0, sizeof(*d));
}

/**
 * @brief get_format callback that forces software decoding by skipping any
 *        hardware pixel formats. On macOS, libav's AV1 decoder otherwise
 *        auto-negotiates VideoToolbox and fails with "Failed to get pixel
 *        format" because we never set up a hwaccel context.
 */
static enum AVPixelFormat force_sw_get_format(AVCodecContext *ctx,
                                              const enum AVPixelFormat *fmts) {
  (void)ctx;
  for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
    if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
      return *p;
  }
  return AV_PIX_FMT_NONE;
}

/**
 * @brief Open @p path, locate its video stream, and prepare a decoder
 *        plus (if necessary) an sws conversion to YUV420P10LE.
 *
 * @return 0 on success, negative AVERROR on failure.
 */
static int decoder_open(Decoder *d, const char *path) {
  memset(d, 0, sizeof(*d));
  d->video_idx = -1;

  int ret = avformat_open_input(&d->fmt_ctx, path, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "ssimu2: cannot open '%s': %s\n", path, av_err2str(ret));
    return ret;
  }
  ret = avformat_find_stream_info(d->fmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "ssimu2: cannot read stream info from '%s': %s\n", path,
            av_err2str(ret));
    goto fail;
  }

  ret = av_find_best_stream(d->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (ret < 0) {
    fprintf(stderr, "ssimu2: no video stream in '%s'\n", path);
    goto fail;
  }
  d->video_idx = ret;

  AVStream *stream = d->fmt_ctx->streams[d->video_idx];
  const AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!dec) {
    fprintf(stderr, "ssimu2: unsupported codec in '%s'\n", path);
    ret = AVERROR_DECODER_NOT_FOUND;
    goto fail;
  }

  d->dec_ctx = avcodec_alloc_context3(dec);
  if (!d->dec_ctx) {
    ret = AVERROR(ENOMEM);
    goto fail;
  }
  ret = avcodec_parameters_to_context(d->dec_ctx, stream->codecpar);
  if (ret < 0)
    goto fail;

  /* Force software decoding — no hwaccel context is set up in this path. */
  d->dec_ctx->get_format = force_sw_get_format;

  ret = avcodec_open2(d->dec_ctx, dec, NULL);
  if (ret < 0) {
    fprintf(stderr, "ssimu2: cannot open decoder for '%s': %s\n", path,
            av_err2str(ret));
    goto fail;
  }

  d->raw = av_frame_alloc();
  d->pkt = av_packet_alloc();
  if (!d->raw || !d->pkt) {
    ret = AVERROR(ENOMEM);
    goto fail;
  }

  /* Conversion target: YUV420P10LE, the pixel format the Rust FFI expects. */
  if (d->dec_ctx->pix_fmt != AV_PIX_FMT_YUV420P10LE) {
    d->sws = sws_getContext(d->dec_ctx->width, d->dec_ctx->height,
                            d->dec_ctx->pix_fmt, d->dec_ctx->width,
                            d->dec_ctx->height, AV_PIX_FMT_YUV420P10LE,
                            SWS_BILINEAR, NULL, NULL, NULL);
    if (!d->sws) {
      fprintf(stderr, "ssimu2: cannot build sws context for '%s'\n", path);
      ret = AVERROR(ENOMEM);
      goto fail;
    }
    d->yuv420p10 = av_frame_alloc();
    if (!d->yuv420p10) {
      ret = AVERROR(ENOMEM);
      goto fail;
    }
    d->yuv420p10->format = AV_PIX_FMT_YUV420P10LE;
    d->yuv420p10->width = d->dec_ctx->width;
    d->yuv420p10->height = d->dec_ctx->height;
    ret = av_frame_get_buffer(d->yuv420p10, 32);
    if (ret < 0)
      goto fail;
  }
  return 0;

fail:
  decoder_close(d);
  return ret;
}

/**
 * @brief Pull the next decoded frame and return it as YUV420P10LE.
 *
 * @return A borrowed pointer to an AVFrame owned by @p d on success,
 *         NULL on EOF, or NULL with @p *err set on error.
 */
static AVFrame *decoder_next(Decoder *d, int *err) {
  *err = 0;
  for (;;) {
    /* Try to drain the decoder first. */
    int ret = avcodec_receive_frame(d->dec_ctx, d->raw);
    if (ret == 0) {
      if (!d->sws)
        return d->raw; /* already yuv420p10le */
      ret = sws_scale(d->sws, (const uint8_t *const *)d->raw->data,
                      d->raw->linesize, 0, d->dec_ctx->height,
                      d->yuv420p10->data, d->yuv420p10->linesize);
      av_frame_unref(d->raw);
      if (ret <= 0) {
        *err = AVERROR_EXTERNAL;
        return NULL;
      }
      return d->yuv420p10;
    }
    if (ret == AVERROR_EOF)
      return NULL;
    if (ret != AVERROR(EAGAIN)) {
      *err = ret;
      return NULL;
    }

    /* Decoder is hungry: feed it another packet. */
    ret = av_read_frame(d->fmt_ctx, d->pkt);
    if (ret == AVERROR_EOF) {
      /* Flush. */
      avcodec_send_packet(d->dec_ctx, NULL);
      continue;
    }
    if (ret < 0) {
      *err = ret;
      return NULL;
    }
    if (d->pkt->stream_index != d->video_idx) {
      av_packet_unref(d->pkt);
      continue;
    }
    ret = avcodec_send_packet(d->dec_ctx, d->pkt);
    av_packet_unref(d->pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
      *err = ret;
      return NULL;
    }
  }
}

/* ====================================================================== */
/*  Statistics helpers                                                    */
/* ====================================================================== */

static int cmp_double(const void *a, const void *b) {
  double da = *(const double *)a, db = *(const double *)b;
  if (da < db)
    return -1;
  if (da > db)
    return 1;
  return 0;
}

/**
 * @brief Populate mean / median / p10 / min / max from an unsorted score
 *        array. The array is sorted in place.
 */
static void finalize_stats(Ssimu2Result *out, double *scores, int n) {
  qsort(scores, (size_t)n, sizeof(double), cmp_double);

  double sum = 0.0;
  for (int i = 0; i < n; i++)
    sum += scores[i];

  int i_p10 = (int)(n * 0.10);
  int i_p50 = (int)(n * 0.50);
  if (i_p10 >= n)
    i_p10 = n - 1;
  if (i_p50 >= n)
    i_p50 = n - 1;

  out->mean = sum / n;
  out->median = scores[i_p50];
  out->p10 = scores[i_p10];
  out->min = scores[0];
  out->max = scores[n - 1];
  out->frames_scored = n;
}

/* ====================================================================== */
/*  Public API                                                            */
/* ====================================================================== */

Ssimu2Result ssimu2_score_files(const char *ref_path, const char *dis_path,
                                int frame_stride) {
  Ssimu2Result result = {0};
  if (frame_stride < 1)
    frame_stride = 1;

  Decoder ref = {0}, dis = {0};
  double *scores = NULL;
  int scores_cap = 0, scores_len = 0;

  int ret = decoder_open(&ref, ref_path);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }
  ret = decoder_open(&dis, dis_path);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  if (ref.dec_ctx->width != dis.dec_ctx->width ||
      ref.dec_ctx->height != dis.dec_ctx->height) {
    fprintf(stderr,
            "ssimu2: resolution mismatch ref=%dx%d dis=%dx%d\n",
            ref.dec_ctx->width, ref.dec_ctx->height,
            dis.dec_ctx->width, dis.dec_ctx->height);
    result.error = AVERROR(EINVAL);
    goto cleanup;
  }

  /* Color metadata for the metric. Taken from the reference: that's the
   * ground-truth signal. Unspecified values (0) are passed through and
   * yuvxyb will guess sensible defaults. */
  const uint32_t cp = (uint32_t)ref.dec_ctx->color_primaries;
  const uint32_t tc = (uint32_t)ref.dec_ctx->color_trc;
  const uint32_t mc = (uint32_t)ref.dec_ctx->colorspace;
  const bool full_range = (ref.dec_ctx->color_range == AVCOL_RANGE_JPEG);

  long frame_index = 0;
  for (;;) {
    int err_r = 0, err_d = 0;
    AVFrame *fr = decoder_next(&ref, &err_r);
    AVFrame *fd = decoder_next(&dis, &err_d);
    if (err_r < 0) {
      result.error = err_r;
      goto cleanup;
    }
    if (err_d < 0) {
      result.error = err_d;
      goto cleanup;
    }
    if (!fr || !fd)
      break; /* EOF on one side — stop. */

    if (frame_index % frame_stride == 0) {
      double score = vmav_ssimu2_score_yuv420p10(
          fr->data[0], fr->data[1], fr->data[2], fd->data[0], fd->data[1],
          fd->data[2], (uint32_t)fr->width, (uint32_t)fr->height,
          (uint32_t)fr->linesize[0], (uint32_t)fr->linesize[1], cp, tc, mc,
          full_range);

      if (score == VMAV_SSIMU2_ERROR) {
        fprintf(stderr, "ssimu2: scoring failed at frame %ld\n", frame_index);
        result.error = AVERROR_EXTERNAL;
        goto cleanup;
      }

      if (scores_len == scores_cap) {
        int new_cap = scores_cap ? scores_cap * 2 : 64;
        double *new_scores =
            (double *)realloc(scores, (size_t)new_cap * sizeof(double));
        if (!new_scores) {
          result.error = AVERROR(ENOMEM);
          goto cleanup;
        }
        scores = new_scores;
        scores_cap = new_cap;
      }
      scores[scores_len++] = score;
    }

    if (fr == ref.raw)
      av_frame_unref(ref.raw);
    if (fd == dis.raw)
      av_frame_unref(dis.raw);
    frame_index++;
  }

  if (scores_len == 0) {
    fprintf(stderr, "ssimu2: no frames scored\n");
    result.error = AVERROR_INVALIDDATA;
    goto cleanup;
  }
  finalize_stats(&result, scores, scores_len);

cleanup:
  free(scores);
  decoder_close(&ref);
  decoder_close(&dis);
  return result;
}
