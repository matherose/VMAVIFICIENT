/**
 * @file media_info.h
 * @brief Media file validation and metadata extraction.
 */

#ifndef MEDIA_INFO_H
#define MEDIA_INFO_H

/**
 * @brief Core video metadata.
 *
 * On success, @c error is 0 and the remaining fields are valid.
 * On failure, @c error holds a negative FFmpeg AVERROR code.
 */
typedef struct {
  int error;        /**< 0 on success, negative AVERROR on failure. */
  int width;        /**< Video width in pixels. */
  int height;       /**< Video height in pixels. */
  double duration;  /**< Duration in seconds (0 if unknown). */
  double framerate; /**< Frames per second (0 if unknown). */
} MediaInfo;

/**
 * @brief Open a media file, validate it, and extract core video metadata.
 *
 * The function performs three validation steps:
 *  1. Opens the container format (detects missing / unreadable files).
 *  2. Probes stream information (detects header corruption).
 *  3. Locates the best video stream (detects audio-only files).
 *
 * Diagnostic messages are printed to @c stderr on failure.
 *
 * @param path  Filesystem path to the media file.
 * @return A @ref MediaInfo struct with the result.
 */
MediaInfo get_media_info(const char *path);

#endif /* MEDIA_INFO_H */
