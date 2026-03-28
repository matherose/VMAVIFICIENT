/**
 * @file media_tracks.h
 * @brief Audio and subtitle track enumeration.
 */

#ifndef MEDIA_TRACKS_H
#define MEDIA_TRACKS_H

#include <stdint.h>

/**
 * @brief Metadata for a single audio or subtitle track.
 */
typedef struct {
  int index;         /**< Stream index in the container. */
  char name[256];    /**< Track title (empty string if absent). */
  char language[64]; /**< ISO 639 language code (empty string if absent). */
  char codec[64];    /**< Short codec name (e.g. "aac", "srt"). */
  int channels;      /**< Number of audio channels (0 for subtitles). */
  int64_t bitrate;   /**< Bitrate in bits/s (0 if unknown). */
  int codec_id;      /**< AVCodecID value (for internal ranking, stored as int). */
  int profile;       /**< Codec profile (e.g. DTS-HD MA vs plain DTS). */
  int is_forced;     /**< 1 if track is forced (disposition or title hint). */
  int is_sdh;        /**< 1 if track is SDH / closed captions. */
} TrackInfo;

/**
 * @brief Collection of audio and subtitle tracks found in a media file.
 *
 * Call @ref free_media_tracks to release the dynamically allocated arrays.
 */
typedef struct {
  int error;            /**< 0 on success, negative AVERROR on failure. */
  int audio_count;      /**< Number of audio tracks. */
  TrackInfo *audio;     /**< Array of audio track descriptors. */
  int subtitle_count;   /**< Number of subtitle tracks. */
  TrackInfo *subtitles; /**< Array of subtitle track descriptors. */
} MediaTracks;

/**
 * @brief Enumerate all audio and subtitle tracks in a media file.
 *
 * @param path  Filesystem path to the media file.
 * @return A @ref MediaTracks struct. Caller must call @ref free_media_tracks.
 */
MediaTracks get_media_tracks(const char *path);

/**
 * @brief Release memory owned by a @ref MediaTracks struct.
 *
 * @param tracks  Pointer to the struct to release. Safe to call on a
 *                zero-initialised or already-freed struct.
 */
void free_media_tracks(MediaTracks *tracks);

/**
 * @brief Select the best audio track for each language present.
 *
 * Compares tracks by codec quality tier, then channel count, then bitrate.
 *
 * @param tracks     A populated MediaTracks (from @ref get_media_tracks).
 * @param out_count  Receives the number of selected tracks.
 * @return Heap-allocated array of TrackInfo (caller must @c free), or NULL.
 */
TrackInfo *select_best_audio_per_language(const MediaTracks *tracks,
                                          int *out_count);

#endif /* MEDIA_TRACKS_H */
