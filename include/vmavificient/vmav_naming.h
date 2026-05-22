#pragma once

#include "vmavificient/vmav_hdr.h"
#include "vmavificient/vmav_result.h"
#include "vmavificient/vmav_tracks.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Output naming follows the scene convention familiar to v1 users:
 *
 *   Inception.2010.1080p.MULTi.VFF.BluRay.x264-GROUP.mkv
 *
 * The schema is title.year.resolution.lang_tag.source.GROUP.mkv with
 * extra feature inserts (.HDR10., .HDR10P., .DV., .Atmos.) when the
 * input warrants them. Pure logic — all detection happens from input
 * filename + media probe results. */

typedef enum {
    VMAV_SOURCE_UNKNOWN = 0,
    VMAV_SOURCE_BDRIP,
    VMAV_SOURCE_BLURAY,
    VMAV_SOURCE_REMUX,
    VMAV_SOURCE_DVDRIP,
    VMAV_SOURCE_DVDREMUX,
    VMAV_SOURCE_WEBRIP,
    VMAV_SOURCE_WEBDL,
    VMAV_SOURCE_WEB,
    VMAV_SOURCE_HDTV,
    VMAV_SOURCE_HDRIP,
    VMAV_SOURCE_TVRIP,
    VMAV_SOURCE_VHSRIP,
} vmav_naming_source_t;

typedef enum {
    VMAV_FR_UNKNOWN = 0,
    VMAV_FR_VFF,
    VMAV_FR_VFQ,
    VMAV_FR_VFI,
} vmav_naming_french_variant_t;

typedef enum {
    VMAV_LT_VO = 0,
    VMAV_LT_VFF,
    VMAV_LT_VOF,
    VMAV_LT_VFQ,
    VMAV_LT_VFI,
    VMAV_LT_MULTI,
    VMAV_LT_MULTI_VFF,
    VMAV_LT_MULTI_VFQ,
    VMAV_LT_MULTI_VFI,
    VMAV_LT_MULTI_VF2,
    VMAV_LT_MULTI_VOF,
    VMAV_LT_DUAL_VFF,
    VMAV_LT_DUAL_VFQ,
    VMAV_LT_DUAL_VFI,
    VMAV_LT_FRENCH,
    VMAV_LT_TRUEFRENCH,
    VMAV_LT_VOST,
    VMAV_LT_FANSUB,
} vmav_naming_lang_tag_t;

/* Detect source type from the input filename. Pattern-matches scene
 * tags (BluRay/BDRip/WEB-DL/HDTV/...). Returns VMAV_SOURCE_UNKNOWN
 * when nothing matches. */
vmav_naming_source_t vmav_naming_detect_source(const char *filename);

/* Detect French variant tag (VFF/VFQ/VFI) from the input filename. */
vmav_naming_french_variant_t vmav_naming_detect_french_variant(const char *filename);

/* Compute the language tag from the audio track list. `original_lang`
 * is the ISO 639-1 code from TMDB (e.g. "en", "fr"); used to
 * distinguish VO (original) from MULTI. `french_variant` should come
 * from vmav_naming_detect_french_variant. */
vmav_naming_lang_tag_t vmav_naming_lang_tag(const vmav_media_tracks_t *tracks,
                                            const char *original_lang_iso1,
                                            vmav_naming_french_variant_t french_variant);

/* Display strings for tags and sources. Never NULL. */
const char *vmav_naming_lang_tag_string(vmav_naming_lang_tag_t tag);
const char *vmav_naming_source_string(vmav_naming_source_t source);

/* Build the output MKV filename into `out` (NUL-terminated). Returns
 * VMAV_ERR_BAD_ARG on null inputs or buffer-too-small. */
vmav_status_t vmav_naming_build(char *out,
                                size_t out_size,
                                const char *title,
                                int year,
                                vmav_naming_lang_tag_t lang_tag,
                                int video_height,
                                const vmav_hdr_info_t *hdr,
                                vmav_naming_source_t source,
                                const char *release_group);

#ifdef __cplusplus
}
#endif
