/**
 * @file media_hdr.h
 * @brief Dolby Vision and HDR10+ detection.
 */

#ifndef MEDIA_HDR_H
#define MEDIA_HDR_H

/**
 * @brief HDR capability flags and metadata for a video stream.
 */
typedef struct {
  int error;            /**< 0 on success, negative AVERROR on failure. */
  int has_dolby_vision; /**< Non-zero if Dolby Vision configuration is present.
                         */
  int dv_profile;       /**< DV profile number (-1 if not present). */
  int dv_level;         /**< DV level number (-1 if not present). */
  int has_hdr10;        /**< Non-zero if PQ transfer (SMPTE ST 2084) is used. */
  int has_hdr10plus;    /**< Non-zero if HDR10+ dynamic metadata is detected. */
} HdrInfo;

/**
 * @brief Detect Dolby Vision and HDR10+ capabilities in a media file.
 *
 * Dolby Vision is detected from codec-level side data (no decoding needed).
 * HDR10+ detection requires decoding a small number of frames and is only
 * attempted when the video uses PQ (SMPTE ST 2084) transfer characteristics.
 *
 * @param path  Filesystem path to the media file.
 * @return A @ref HdrInfo struct with the result.
 */
HdrInfo get_hdr_info(const char *path);

#endif /* MEDIA_HDR_H */
