/**
 * @file media_tracks.c
 * @brief Implementation of audio and subtitle track enumeration.
 */

#include "media_tracks.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/codec_id.h>
#include <libavcodec/defs.h>
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

  t->channels = stream->codecpar->ch_layout.nb_channels;
  t->bitrate = stream->codecpar->bit_rate;
  t->codec_id = stream->codecpar->codec_id;
  t->profile = stream->codecpar->profile;
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

static int codec_quality_rank(int codec_id, int profile) {
  switch (codec_id) {
  case AV_CODEC_ID_TRUEHD:
    return 60;
  case AV_CODEC_ID_DTS:
    if (profile == AV_PROFILE_DTS_HD_MA ||
        profile == AV_PROFILE_DTS_HD_MA_X ||
        profile == AV_PROFILE_DTS_HD_MA_X_IMAX)
      return 50;
    if (profile == AV_PROFILE_DTS_HD_HRA)
      return 35;
    return 20;
  case AV_CODEC_ID_FLAC:
    return 45;
  case AV_CODEC_ID_PCM_S16LE:
  case AV_CODEC_ID_PCM_S24LE:
  case AV_CODEC_ID_PCM_S32LE:
    return 40;
  case AV_CODEC_ID_EAC3:
    return 30;
  case AV_CODEC_ID_AC3:
    return 25;
  case AV_CODEC_ID_AAC:
    return 15;
  case AV_CODEC_ID_MP3:
    return 10;
  case AV_CODEC_ID_MP2:
    return 5;
  default:
    return 0;
  }
}

static bool track_is_better(const TrackInfo *a, const TrackInfo *b) {
  int ra = codec_quality_rank(a->codec_id, a->profile);
  int rb = codec_quality_rank(b->codec_id, b->profile);
  if (ra != rb)
    return ra > rb;
  if (a->channels != b->channels)
    return a->channels > b->channels;
  return a->bitrate > b->bitrate;
}

TrackInfo *select_best_audio_per_language(const MediaTracks *tracks,
                                          int *out_count) {
  *out_count = 0;
  if (!tracks || tracks->audio_count == 0)
    return NULL;

  TrackInfo *best = calloc(tracks->audio_count, sizeof(TrackInfo));
  if (!best)
    return NULL;

  int count = 0;
  for (int i = 0; i < tracks->audio_count; i++) {
    const TrackInfo *t = &tracks->audio[i];
    const char *lang = t->language[0] ? t->language : "und";

    /* Check if we already have an entry for this language. */
    int found = -1;
    for (int j = 0; j < count; j++) {
      const char *blang = best[j].language[0] ? best[j].language : "und";
      if (strcmp(lang, blang) == 0) {
        found = j;
        break;
      }
    }

    if (found < 0) {
      best[count++] = *t;
    } else if (track_is_better(t, &best[found])) {
      best[found] = *t;
    }
  }

  *out_count = count;
  return best;
}
