/**
 * @file media_info.c
 * @brief Implementation of media file validation and metadata extraction.
 */

#include "media_info.h"

#include <stdio.h>

#include <libavcodec/codec_par.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/rational.h>

MediaInfo get_media_info(const char *path) {
  MediaInfo info = {
      .error = 0, .width = 0, .height = 0, .duration = 0.0, .framerate = 0.0};
  AVFormatContext *fmt_ctx = NULL;
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  int ret;

  ret = avformat_open_input(&fmt_ctx, path, NULL, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "Error: cannot open '%s': %s\n", path, errbuf);
    info.error = ret;
    return info;
  }

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "Error: cannot read stream info from '%s': %s\n", path,
            errbuf);
    info.error = ret;
    avformat_close_input(&fmt_ctx);
    return info;
  }

  ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "Error: no video stream found in '%s': %s\n", path, errbuf);
    info.error = ret;
    avformat_close_input(&fmt_ctx);
    return info;
  }

  AVStream *stream = fmt_ctx->streams[ret];
  AVCodecParameters *codecpar = stream->codecpar;

  info.width = codecpar->width;
  info.height = codecpar->height;

  if (fmt_ctx->duration > 0)
    info.duration = (double)fmt_ctx->duration / AV_TIME_BASE;

  if (stream->avg_frame_rate.den > 0)
    info.framerate = av_q2d(stream->avg_frame_rate);
  else if (stream->r_frame_rate.den > 0)
    info.framerate = av_q2d(stream->r_frame_rate);

  avformat_close_input(&fmt_ctx);
  return info;
}
