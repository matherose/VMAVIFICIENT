/**
 * @file media_tracks.h
 * @brief Audio and subtitle track enumeration.
 */

#ifndef MEDIA_TRACKS_H
#define MEDIA_TRACKS_H

/**
 * @brief Metadata for a single audio or subtitle track.
 */
typedef struct {
  int index;         /**< Stream index in the container. */
  char name[256];    /**< Track title (empty string if absent). */
  char language[64]; /**< ISO 639 language code (empty string if absent). */
  char codec[64];    /**< Short codec name (e.g. "aac", "srt"). */
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

#endif /* MEDIA_TRACKS_H */
