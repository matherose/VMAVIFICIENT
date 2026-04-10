/**
 * @file video_encode.h
 * @brief AV1 video encoding via SVT-AV1-HDR with Dolby Vision / HDR10+ support.
 */

#ifndef VIDEO_ENCODE_H
#define VIDEO_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#include "encode_preset.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"

/**
 * @brief Configuration for a video encode job.
 */
typedef struct {
  const char *input_path;     /**< Source media file. */
  const char *output_path;    /**< Output .mkv path. */
  const char *rpu_path;       /**< Dolby Vision RPU .bin (NULL if none). */
  const EncodePreset *preset; /**< SVT-AV1 parameter preset. */
  int film_grain;             /**< Film grain synthesis level (0–50). */
  int target_bitrate;         /**< Target bitrate in kbps (VBR mode, ignored if crf > 0). */
  int crf;                    /**< CRF value 1–63 for CRF mode, or 0 to use VBR @ target_bitrate. */
  const MediaInfo *info;      /**< Source media info. */
  const CropInfo *crop;       /**< Crop values (NULL if none). */
  const HdrInfo *hdr;         /**< HDR info. */
} VideoEncodeConfig;

/**
 * @brief Result of a video encode operation.
 */
typedef struct {
  int error;              /**< 0 on success, negative on failure. */
  int skipped;            /**< 1 if output already exists. */
  int64_t frames_encoded; /**< Number of frames encoded. */
  int64_t bytes_written;  /**< Output file size in bytes. */
} VideoEncodeResult;

/**
 * @brief Encode video to AV1 in MKV container using SVT-AV1-HDR.
 *
 * Decodes the source video with FFmpeg, pipes frames through SVT-AV1-HDR,
 * attaches Dolby Vision RPU metadata per-frame (if rpu_path is set),
 * and muxes the output into an MKV container.
 *
 * @param config  Encoding configuration.
 * @return Result with error status and statistics.
 */
VideoEncodeResult encode_video(const VideoEncodeConfig *config);

#endif /* VIDEO_ENCODE_H */
