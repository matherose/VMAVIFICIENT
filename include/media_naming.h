/**
 * @file media_naming.h
 * @brief Standardized output filename generation.
 */

#ifndef MEDIA_NAMING_H
#define MEDIA_NAMING_H

#include <stddef.h>

#include "media_hdr.h"
#include "media_info.h"
#include "media_tracks.h"

typedef enum {
  LANG_TAG_VO,
  LANG_TAG_VFF,
  LANG_TAG_VOF,
  LANG_TAG_VFQ,
  LANG_TAG_VFI,
  LANG_TAG_MULTI_VFF,
  LANG_TAG_MULTI_VFQ,
  LANG_TAG_MULTI_VFI,
  LANG_TAG_MULTI,
} LanguageTag;

typedef enum {
  SOURCE_BLURAY,
  SOURCE_WEBDL,
  SOURCE_WEBRIP,
  SOURCE_UNKNOWN,
} SourceType;

typedef enum {
  FRENCH_VARIANT_UNKNOWN,
  FRENCH_VARIANT_VFF,
  FRENCH_VARIANT_VFQ,
  FRENCH_VARIANT_VFI,
} FrenchVariant;

/**
 * @brief Convert ISO 639-1 (2-letter) to ISO 639-2/B (3-letter) code.
 * @return Static string or NULL if not found.
 */
const char *iso639_1_to_2b(const char *iso1);

/**
 * @brief Determine the language tag from audio tracks and original language.
 */
LanguageTag determine_language_tag(const MediaTracks *tracks,
                                   const char *original_lang_iso1,
                                   FrenchVariant french_variant);

/**
 * @brief Detect French variant (VFF/VFQ/VFI) from the input filename.
 */
FrenchVariant detect_french_variant_from_filename(const char *filename);

/**
 * @brief Detect source type from the input filename.
 */
SourceType detect_source_from_filename(const char *filename);

/**
 * @brief Convert a language tag enum to its display string.
 */
const char *language_tag_to_string(LanguageTag tag);

/**
 * @brief Build the standardized output filename.
 *
 * Format: TITLE.YEAR.LANGUAGES.RESOLUTION.FEATURE.SOURCE.QUALITY.10bit.AV1.OPUS-matherose.mkv
 *
 * @param buf       Output buffer.
 * @param bufsize   Size of output buffer.
 * @param title     Original movie title (from TMDB).
 * @param year      Release year.
 * @param lang_tag  Language tag.
 * @param info      Media info (for resolution).
 * @param hdr       HDR info (for features).
 * @param source    Source type.
 * @return 0 on success, -1 on error.
 */
int build_output_filename(char *buf, size_t bufsize, const char *title,
                          int year, LanguageTag lang_tag, const MediaInfo *info,
                          const HdrInfo *hdr, SourceType source);

#endif /* MEDIA_NAMING_H */
