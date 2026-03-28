/**
 * @file media_crop.h
 * @brief Automatic crop detection (Handbrake style).
 */

#ifndef MEDIA_CROP_H
#define MEDIA_CROP_H

/**
 * @brief Detected crop values in Handbrake format.
 *
 * All values are in pixels. A fully uncropped frame has all fields at zero.
 */
typedef struct {
  int error;  /**< 0 on success, negative AVERROR on failure. */
  int top;    /**< Pixels to crop from the top edge. */
  int bottom; /**< Pixels to crop from the bottom edge. */
  int left;   /**< Pixels to crop from the left edge. */
  int right;  /**< Pixels to crop from the right edge. */
} CropInfo;

/**
 * @brief Detect black borders using FFmpeg's cropdetect filter.
 *
 * Samples 10 frames distributed across the middle 80 %% of the video and
 * returns the median crop for each edge. This mirrors HandBrake's approach
 * of multi-frame sampling to avoid false positives from transition frames.
 *
 * @param path  Filesystem path to the media file.
 * @return A @ref CropInfo struct with the result.
 */
CropInfo get_crop_info(const char *path);

#endif /* MEDIA_CROP_H */
