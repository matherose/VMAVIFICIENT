/**
 * @file rpu_extract.h
 * @brief Dolby Vision RPU extraction from HEVC streams.
 */

#ifndef RPU_EXTRACT_H
#define RPU_EXTRACT_H

#include <stddef.h>

/** @brief Result of an RPU extraction operation. */
typedef struct {
  int error;              /**< 0 on success, negative on failure. */
  char output_path[2048]; /**< Path to the generated .rpu.bin file. */
  int skipped;            /**< 1 if file already existed and was reused. */
  int rpu_count;          /**< Number of RPU NALUs extracted. */
} RpuExtractResult;

/**
 * @brief Extract Dolby Vision RPU data from a HEVC video file.
 *
 * Demuxes the video stream, locates UNSPEC62 NAL units containing
 * DV RPU data, parses them with libdovi and writes the result to
 * a binary RPU file (length-prefixed format compatible with dovi_tool).
 *
 * If @p output_path already exists, the extraction is skipped and
 * RpuExtractResult::skipped is set to 1.
 *
 * @param input_path  Path to the source media file.
 * @param output_path Full path for the output .rpu.bin file.
 * @return Result with error status and RPU count.
 */
RpuExtractResult extract_rpu(const char *input_path, const char *output_path);

/**
 * @brief Build the RPU output filename for a video.
 *
 * Format: <base_name>.rpu.bin
 *
 * @param buf       Output buffer.
 * @param bufsize   Size of output buffer.
 * @param base_name Base filename (without .mkv extension).
 */
void build_rpu_filename(char *buf, size_t bufsize, const char *base_name);

#endif /* RPU_EXTRACT_H */
