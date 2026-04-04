/**
 * @file subtitle_convert.h
 * @brief PGS bitmap subtitle to SRT text conversion using Tesseract OCR.
 */

#ifndef SUBTITLE_CONVERT_H
#define SUBTITLE_CONVERT_H

#include <stddef.h>

#include "media_tracks.h"

/** @brief Result of a PGS-to-SRT conversion. */
typedef struct {
  int error;              /**< 0 on success, negative on failure. */
  char output_path[2048]; /**< Path to the generated .srt file. */
  int skipped;            /**< 1 if SRT already existed and was reused. */
  int subtitle_count;     /**< Number of subtitle events converted. */
} SubtitleConvertResult;

/**
 * @brief Convert a PGS (HDMV) bitmap subtitle track to SRT using Tesseract OCR.
 *
 * Decodes PGS subtitle bitmaps with FFmpeg, performs OCR on each image
 * using libtesseract, and writes the result as an SRT file with timestamps.
 *
 * If @p output_path already exists, the conversion is skipped and
 * SubtitleConvertResult::skipped is set to 1.
 *
 * @param input_path   Path to the source media file.
 * @param track        Subtitle track info (stream index, language).
 * @param output_path  Full path for the output .srt file.
 * @param tesseract_lang  Tesseract language code (e.g. "eng", "fra").
 *                        If NULL, inferred from track language.
 * @return Result with error status and subtitle count.
 */
SubtitleConvertResult convert_pgs_to_srt(const char *input_path,
                                         const TrackInfo *track,
                                         const char *output_path,
                                         const char *tesseract_lang);

/**
 * @brief Check if a subtitle track is PGS (bitmap-based).
 *
 * @param track  Subtitle track info.
 * @return 1 if PGS, 0 otherwise.
 */
int is_pgs_subtitle(const TrackInfo *track);

/**
 * @brief Check if a subtitle track is text-based (SRT, ASS, etc.).
 *
 * @param track  Subtitle track info.
 * @return 1 if text-based, 0 otherwise.
 */
int is_text_subtitle(const TrackInfo *track);

/**
 * @brief Map ISO 639-2/B language code to Tesseract language code.
 *
 * Most codes are identical (eng, fra, deu, spa...) but some differ.
 *
 * @param iso639  3-letter ISO 639-2/B code.
 * @return Tesseract language string (static, never NULL).
 */
const char *iso639_to_tesseract_lang(const char *iso639);

#endif /* SUBTITLE_CONVERT_H */
