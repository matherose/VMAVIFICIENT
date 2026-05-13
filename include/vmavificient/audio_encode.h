/**
 * @file audio_encode.h
 * @brief Audio track encoding to OPUS format.
 */

#ifndef AUDIO_ENCODE_H
#define AUDIO_ENCODE_H

#include <stddef.h>

#include "media_naming.h"
#include "media_tracks.h"

/** @brief Result of a single track OPUS encoding operation. */
typedef struct {
  int error;              /**< 0 on success, negative on failure. */
  char output_path[2048]; /**< Path to the generated .opus file. */
  int skipped;            /**< 1 if file already existed and was reused. */
} OpusEncodeResult;

/**
 * @brief Encode a single audio track to OPUS.
 *
 * If @p output_path already exists, the encode is skipped and
 * OpusEncodeResult::skipped is set to 1.
 *
 * Encoding parameters:
 *   - Bitrate: 64 kbps per channel
 *   - Sample rate: 48 kHz
 *   - Application: audio
 *   - VBR: constrained
 *   - Compression level: 10
 *   - Channel layout: preserved from source
 *
 * @param input_path  Path to the source media file.
 * @param track       Track info (stream index, channels, language).
 * @param output_path Full path for the output .opus file.
 * @return Result with error status and output path.
 */
OpusEncodeResult encode_track_to_opus(const char *input_path,
                                      const TrackInfo *track,
                                      const char *output_path);

/**
 * @brief Build the OPUS output filename for a track.
 *
 * French tracks use a country suffix:
 *   VFF -> .fre.fr.opus
 *   VFQ -> .fre.ca.opus
 *   VFI -> .fre.vfi.opus
 * Other languages: .<lang3>.opus
 *
 * @param buf       Output buffer.
 * @param bufsize   Size of output buffer.
 * @param base_name Base filename (without .mkv extension).
 * @param language  ISO 639-2/B 3-letter language code.
 * @param fv        French variant (only used if language is French).
 */
void build_opus_filename(char *buf, size_t bufsize, const char *base_name,
                         const char *language, FrenchVariant fv);

/**
 * @brief Verify an OPUS file is valid by probing with FFmpeg.
 *
 * @param path Path to the .opus file.
 * @return 0 if valid, negative error code on failure.
 */
int verify_opus_file(const char *path);

#endif /* AUDIO_ENCODE_H */
