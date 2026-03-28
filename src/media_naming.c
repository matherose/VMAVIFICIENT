/**
 * @file media_naming.c
 * @brief Implementation of standardized output filename generation.
 */

#include "media_naming.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ── ISO 639-1 → 639-2/B mapping ──────────────────────────────── */

static const struct {
  const char *iso1;
  const char *iso2b;
} iso_map[] = {
    {"en", "eng"}, {"fr", "fre"}, {"de", "ger"}, {"es", "spa"},
    {"it", "ita"}, {"ja", "jpn"}, {"ko", "kor"}, {"zh", "chi"},
    {"pt", "por"}, {"ru", "rus"}, {"ar", "ara"}, {"hi", "hin"},
    {"nl", "dut"}, {"sv", "swe"}, {"no", "nor"}, {"da", "dan"},
    {"fi", "fin"}, {"tr", "tur"}, {"pl", "pol"}, {"cs", "cze"},
    {"hu", "hun"}, {"ro", "rum"}, {"th", "tha"}, {"vi", "vie"},
    {"el", "gre"}, {"he", "heb"}, {"id", "ind"}, {"ms", "may"},
    {"uk", "ukr"}, {"bg", "bul"}, {"hr", "hrv"}, {"sk", "slo"},
    {NULL, NULL},
};

const char *iso639_1_to_2b(const char *iso1) {
  if (!iso1)
    return NULL;
  for (int i = 0; iso_map[i].iso1; i++) {
    if (strcmp(iso1, iso_map[i].iso1) == 0)
      return iso_map[i].iso2b;
  }
  return NULL;
}

/**
 * @brief Check if a 3-letter language code is French (639-2/B or 639-2/T).
 */
static bool is_french_code(const char *code) {
  return strcmp(code, "fre") == 0 || strcmp(code, "fra") == 0;
}

/* ── Language tag determination ────────────────────────────────── */

LanguageTag determine_language_tag(const MediaTracks *tracks,
                                   const char *original_lang_iso1,
                                   FrenchVariant french_variant) {
  if (!tracks || tracks->audio_count == 0)
    return LANG_TAG_VO;

  const char *orig_3 = iso639_1_to_2b(original_lang_iso1);
  bool orig_is_french =
      original_lang_iso1 && strcmp(original_lang_iso1, "fr") == 0;

  bool has_original = false;
  bool has_french = false;
  int lang_count = 0;

  /* Collect distinct languages. */
  char seen[32][64];
  int seen_count = 0;

  for (int i = 0; i < tracks->audio_count; i++) {
    const char *lang =
        tracks->audio[i].language[0] ? tracks->audio[i].language : "und";

    /* Check if this language was already counted. */
    bool dup = false;
    for (int j = 0; j < seen_count; j++) {
      if (strcmp(seen[j], lang) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup && seen_count < 32) {
      snprintf(seen[seen_count], sizeof(seen[0]), "%s", lang);
      seen_count++;
    }

    if (orig_3 && (strcmp(lang, orig_3) == 0))
      has_original = true;
    /* Also handle 639-2/T for original language match. */
    if (orig_is_french && is_french_code(lang))
      has_original = true;
    if (is_french_code(lang))
      has_french = true;
  }

  lang_count = seen_count;

  /* If original is French, treat original and french as the same group. */
  if (orig_is_french) {
    if (lang_count == 1 && has_french)
      return LANG_TAG_VOF;
    if (lang_count > 1 && has_french) {
      switch (french_variant) {
      case FRENCH_VARIANT_VFQ:
        return LANG_TAG_MULTI_VFQ;
      case FRENCH_VARIANT_VFI:
        return LANG_TAG_MULTI_VFI;
      default:
        return LANG_TAG_MULTI_VFF;
      }
    }
  }

  /* Only original language, no French. */
  if (lang_count == 1 && has_original && !has_french)
    return LANG_TAG_VO;

  /* Only French, original is not French. */
  if (lang_count == 1 && has_french && !orig_is_french) {
    switch (french_variant) {
    case FRENCH_VARIANT_VFQ:
      return LANG_TAG_VFQ;
    case FRENCH_VARIANT_VFI:
      return LANG_TAG_VFI;
    default:
      return LANG_TAG_VFF;
    }
  }

  /* Multiple languages including French. */
  if (has_french && !orig_is_french) {
    switch (french_variant) {
    case FRENCH_VARIANT_VFQ:
      return LANG_TAG_MULTI_VFQ;
    case FRENCH_VARIANT_VFI:
      return LANG_TAG_MULTI_VFI;
    default:
      return LANG_TAG_MULTI_VFF;
    }
  }

  /* Multiple languages, no French. */
  if (lang_count > 1)
    return LANG_TAG_MULTI;

  return LANG_TAG_VO;
}

/* ── French variant from filename ──────────────────────────────── */

FrenchVariant detect_french_variant_from_filename(const char *filename) {
  if (!filename)
    return FRENCH_VARIANT_UNKNOWN;

  /* Check specific tags first (VFQ/VFI before VFF). */
  if (str_contains_ci(filename, "VFQ"))
    return FRENCH_VARIANT_VFQ;
  if (str_contains_ci(filename, "VFI"))
    return FRENCH_VARIANT_VFI;
  if (str_contains_ci(filename, "VFF") ||
      str_contains_ci(filename, "TRUEFRENCH"))
    return FRENCH_VARIANT_VFF;

  return FRENCH_VARIANT_UNKNOWN;
}

/* ── Source detection from filename ────────────────────────────── */

SourceType detect_source_from_filename(const char *filename) {
  if (!filename)
    return SOURCE_UNKNOWN;

  if (str_contains_ci(filename, "BLURAY") ||
      str_contains_ci(filename, "BLU-RAY") ||
      str_contains_ci(filename, "BDREMUX") ||
      str_contains_ci(filename, "REMUX"))
    return SOURCE_BLURAY;

  if (str_contains_ci(filename, "WEBDL") ||
      str_contains_ci(filename, "WEB-DL") ||
      str_contains_ci(filename, "WEB DL"))
    return SOURCE_WEBDL;

  if (str_contains_ci(filename, "WEBRIP") ||
      str_contains_ci(filename, "WEB-RIP") ||
      str_contains_ci(filename, "WEB RIP"))
    return SOURCE_WEBRIP;

  return SOURCE_UNKNOWN;
}

/* ── String helpers ────────────────────────────────────────────── */

const char *language_tag_to_string(LanguageTag tag) {
  switch (tag) {
  case LANG_TAG_VO:
    return "VO";
  case LANG_TAG_VFF:
    return "VFF";
  case LANG_TAG_VOF:
    return "VOF";
  case LANG_TAG_VFQ:
    return "VFQ";
  case LANG_TAG_VFI:
    return "VFI";
  case LANG_TAG_MULTI_VFF:
    return "MULTI.VFF";
  case LANG_TAG_MULTI_VFQ:
    return "MULTI.VFQ";
  case LANG_TAG_MULTI_VFI:
    return "MULTI.VFI";
  case LANG_TAG_MULTI:
    return "MULTI";
  }
  return "VO";
}

static const char *source_to_string(SourceType source) {
  switch (source) {
  case SOURCE_BLURAY:
    return "BluRay";
  case SOURCE_WEBDL:
    return "WEBDL";
  case SOURCE_WEBRIP:
    return "WEBRip";
  case SOURCE_UNKNOWN:
    return "Unknown";
  }
  return "Unknown";
}

/**
 * @brief Sanitize a title for use in a filename.
 *
 * Replaces spaces with dots. Strips characters unsafe for filesystems.
 */
static void sanitize_title(char *out, size_t outsize, const char *title) {
  size_t j = 0;
  for (size_t i = 0; title[i] && j < outsize - 1; i++) {
    char c = title[i];
    if (c == ' ')
      out[j++] = '.';
    else if (c == ':' || c == '/' || c == '\\' || c == '?' || c == '*' ||
             c == '"' || c == '<' || c == '>' || c == '|')
      continue; /* skip unsafe chars */
    else
      out[j++] = c;
  }
  out[j] = '\0';
}

/* ── Filename builder ──────────────────────────────────────────── */

int build_output_filename(char *buf, size_t bufsize, const char *title,
                          int year, LanguageTag lang_tag, const MediaInfo *info,
                          const HdrInfo *hdr, SourceType source) {
  char safe_title[512];
  sanitize_title(safe_title, sizeof(safe_title), title);

  const char *resolution = info->height >= 2160 ? "2160p" : "1080p";
  const char *quality = info->height >= 2160 ? "4KLight" : "HDLight";

  /* Best HDR feature wins: DV > HDR10+ > HDR10 > SDR. */
  char feature[64] = "SDR";
  if (hdr && hdr->error == 0) {
    if (hdr->has_dolby_vision)
      snprintf(feature, sizeof(feature), "DV");
    else if (hdr->has_hdr10plus)
      snprintf(feature, sizeof(feature), "HDR10Plus");
    else if (hdr->has_hdr10)
      snprintf(feature, sizeof(feature), "HDR10");
  }

  /* Assemble: TITLE.YEAR.LANG.RES.FEATURE.SOURCE.QUALITY.10bit.AV1.OPUS-matherose.mkv */
  snprintf(buf, bufsize, "%s.%d.%s.%s.%s.%s.%s.10bit.AV1.OPUS-matherose.mkv",
           safe_title, year, language_tag_to_string(lang_tag), resolution,
           feature, source_to_string(source), quality);

  return 0;
}
