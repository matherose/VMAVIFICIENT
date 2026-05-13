/**
 * @file final_mux.h
 * @brief Final MKV muxing: video + audio + subtitles with track names.
 */

#ifndef FINAL_MUX_H
#define FINAL_MUX_H

#include <stddef.h>

#include "media_tracks.h"

/** @brief Descriptor for an audio track to be muxed. */
typedef struct {
  const char *path;       /**< Path to the .opus file. */
  const char *language;   /**< ISO 639-2/B language code. */
  const char *track_name; /**< Display name (e.g. "English [5.1]"). */
  int is_default;         /**< 1 if this should be the default track. */
} MuxAudioTrack;

/** @brief Descriptor for a subtitle track to be muxed. */
typedef struct {
  const char *path;       /**< Path to the .srt file. */
  const char *language;   /**< ISO 639-2/B language code. */
  const char *track_name; /**< Display name (e.g. "Français | SRT (full)"). */
  int is_default;         /**< 1 if this should be the default track. */
  int is_forced;          /**< 1 if forced subtitle. */
  int is_sdh;             /**< 1 if SDH / hearing-impaired subtitle. */
} MuxSubtitleTrack;

/** @brief Configuration for the final MKV mux. */
typedef struct {
  const char *video_path;       /**< Path to the video-only .mkv. */
  const char *output_path;      /**< Final output .mkv path. */
  const MuxAudioTrack *audio;   /**< Array of audio tracks. */
  int audio_count;              /**< Number of audio tracks. */
  const MuxSubtitleTrack *subs; /**< Array of subtitle tracks. */
  int sub_count;                /**< Number of subtitle tracks. */
  const char *title;            /**< MKV container title, or NULL. */
  const char *video_title;      /**< Video stream title, or NULL. */
  const char *video_language;   /**< Video stream ISO 639-2/B code, or NULL. */
  const char *chapters_source_path; /**< File to copy chapters from, or NULL. */
} FinalMuxConfig;

/** @brief Result of the final mux operation. */
typedef struct {
  int error;   /**< 0 on success, negative on failure. */
  int skipped; /**< 1 if output already exists. */
} FinalMuxResult;

/**
 * @brief Mux video, audio, and subtitle tracks into a final MKV.
 *
 * Uses FFmpeg muxing API to combine:
 *   - Video from the AV1-encoded MKV
 *   - OPUS audio tracks with proper display names and language tags
 *   - SRT subtitle tracks with proper display names and language tags
 *
 * Track names and language metadata are set on each output stream.
 *
 * @param config  Muxing configuration.
 * @return Result with error status.
 */
FinalMuxResult final_mux(const FinalMuxConfig *config);

#endif /* FINAL_MUX_H */
