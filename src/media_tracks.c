/**
 * @file media_tracks.c
 * @brief Implementation of audio and subtitle track enumeration.
 */

#include "media_tracks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>

/**
 * @brief Fill a TrackInfo from an AVStream.
 */
static void fill_track(TrackInfo *t, AVStream *stream) {
  t->index = stream->index;

  AVDictionaryEntry *tag;

  tag = av_dict_get(stream->metadata, "title", NULL, 0);
  if (tag)
    snprintf(t->name, sizeof(t->name), "%s", tag->value);
  else
    t->name[0] = '\0';

  tag = av_dict_get(stream->metadata, "language", NULL, 0);
  if (tag)
    snprintf(t->language, sizeof(t->language), "%s", tag->value);
  else
    t->language[0] = '\0';

  snprintf(t->codec, sizeof(t->codec), "%s",
           avcodec_get_name(stream->codecpar->codec_id));
}

MediaTracks get_media_tracks(const char *path) {
  MediaTracks result = {0};
  AVFormatContext *fmt_ctx = NULL;
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
    avformat_close_input(&fmt_ctx);
    return result;
  }

  /* Count tracks first to allocate arrays. */
  int n_audio = 0, n_sub = 0;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
      n_audio++;
    else if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
      n_sub++;
  }

  if (n_audio > 0) {
    result.audio = calloc(n_audio, sizeof(TrackInfo));
    if (!result.audio) {
      result.error = AVERROR(ENOMEM);
      avformat_close_input(&fmt_ctx);
      return result;
    }
  }

  if (n_sub > 0) {
    result.subtitles = calloc(n_sub, sizeof(TrackInfo));
    if (!result.subtitles) {
      free(result.audio);
      result.audio = NULL;
      result.error = AVERROR(ENOMEM);
      avformat_close_input(&fmt_ctx);
      return result;
    }
  }

  int ai = 0, si = 0;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
    AVStream *s = fmt_ctx->streams[i];
    if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
      fill_track(&result.audio[ai++], s);
    else if (s->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
      fill_track(&result.subtitles[si++], s);
  }

  result.audio_count = n_audio;
  result.subtitle_count = n_sub;

  avformat_close_input(&fmt_ctx);
  return result;
}

void free_media_tracks(MediaTracks *tracks) {
  if (!tracks)
    return;
  free(tracks->audio);
  tracks->audio = NULL;
  tracks->audio_count = 0;
  free(tracks->subtitles);
  tracks->subtitles = NULL;
  tracks->subtitle_count = 0;
}
