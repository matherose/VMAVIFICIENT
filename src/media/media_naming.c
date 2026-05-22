#include "vmavificient/vmav_naming.h"

#include "util/str_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

vmav_naming_source_t vmav_naming_detect_source(const char *filename) {
    if (filename == NULL) {
        return VMAV_SOURCE_UNKNOWN;
    }
    /* Order matters: more specific patterns first. */
    if (vmav_str_contains_ci(filename, "BDREMUX") || vmav_str_contains_ci(filename, "BD-REMUX")) {
        return VMAV_SOURCE_REMUX;
    }
    if (vmav_str_contains_ci(filename, "DVDREMUX") || vmav_str_contains_ci(filename, "DVD-REMUX")) {
        return VMAV_SOURCE_DVDREMUX;
    }
    if (vmav_str_contains_ci(filename, "BDRIP") || vmav_str_contains_ci(filename, "BD-RIP")) {
        return VMAV_SOURCE_BDRIP;
    }
    if (vmav_str_contains_ci(filename, "DVDRIP") || vmav_str_contains_ci(filename, "DVD-RIP")) {
        return VMAV_SOURCE_DVDRIP;
    }
    if (vmav_str_contains_ci(filename, "BLURAY") || vmav_str_contains_ci(filename, "BLU-RAY")) {
        return VMAV_SOURCE_BLURAY;
    }
    if (vmav_str_contains_ci(filename, "REMUX")) {
        return VMAV_SOURCE_REMUX;
    }
    if (vmav_str_contains_ci(filename, "WEBDL") || vmav_str_contains_ci(filename, "WEB-DL")) {
        return VMAV_SOURCE_WEBDL;
    }
    if (vmav_str_contains_ci(filename, "WEBRIP") || vmav_str_contains_ci(filename, "WEB-RIP")) {
        return VMAV_SOURCE_WEBRIP;
    }
    if (vmav_str_contains_ci(filename, "WEB")) {
        return VMAV_SOURCE_WEB;
    }
    if (vmav_str_contains_ci(filename, "HDTV")) {
        return VMAV_SOURCE_HDTV;
    }
    if (vmav_str_contains_ci(filename, "HDRIP") || vmav_str_contains_ci(filename, "HD-RIP")) {
        return VMAV_SOURCE_HDRIP;
    }
    if (vmav_str_contains_ci(filename, "TVRIP") || vmav_str_contains_ci(filename, "TV-RIP")) {
        return VMAV_SOURCE_TVRIP;
    }
    if (vmav_str_contains_ci(filename, "VHSRIP") || vmav_str_contains_ci(filename, "VHS-RIP")) {
        return VMAV_SOURCE_VHSRIP;
    }
    return VMAV_SOURCE_UNKNOWN;
}

vmav_naming_french_variant_t vmav_naming_detect_french_variant(const char *filename) {
    if (filename == NULL) {
        return VMAV_FR_UNKNOWN;
    }
    /* Specific tags first (VFQ/VFI before VFF) to avoid partial-match
     * collisions. */
    if (vmav_str_contains_ci(filename, "VFQ")) {
        return VMAV_FR_VFQ;
    }
    if (vmav_str_contains_ci(filename, "VFI")) {
        return VMAV_FR_VFI;
    }
    if (vmav_str_contains_ci(filename, "VFF") || vmav_str_contains_ci(filename, "TRUEFRENCH")) {
        return VMAV_FR_VFF;
    }
    return VMAV_FR_UNKNOWN;
}

static bool is_french_code(const char *code) {
    return code != NULL && (strcmp(code, "fre") == 0 || strcmp(code, "fra") == 0);
}

static bool is_english_code(const char *code) {
    return code != NULL && (strcmp(code, "eng") == 0 || strcmp(code, "en") == 0);
}

vmav_naming_lang_tag_t vmav_naming_lang_tag(const vmav_media_tracks_t *tracks,
                                            const char *original_lang_iso1,
                                            vmav_naming_french_variant_t french_variant) {
    if (tracks == NULL) {
        return VMAV_LT_VO;
    }
    bool has_fr = false;
    bool has_en = false;
    int other_lang_count = 0;
    for (size_t i = 0; i < tracks->audio_count; i++) {
        const char *l = tracks->audio[i].language;
        if (is_french_code(l)) {
            has_fr = true;
        } else if (is_english_code(l)) {
            has_en = true;
        } else if (l[0] != '\0') {
            other_lang_count++;
        }
    }
    const bool original_is_french =
        is_french_code(original_lang_iso1) ||
        (original_lang_iso1 != NULL && strcmp(original_lang_iso1, "fr") == 0);

    /* Multi (3+ distinct languages including FR + EN). */
    if (has_fr && has_en && other_lang_count >= 1) {
        switch (french_variant) {
        case VMAV_FR_VFF:
            return VMAV_LT_MULTI_VFF;
        case VMAV_FR_VFQ:
            return VMAV_LT_MULTI_VFQ;
        case VMAV_FR_VFI:
            return VMAV_LT_MULTI_VFI;
        case VMAV_FR_UNKNOWN:
        default:
            return VMAV_LT_MULTI;
        }
    }
    /* Dual (FR + EN only, no other languages). */
    if (has_fr && has_en) {
        switch (french_variant) {
        case VMAV_FR_VFF:
            return VMAV_LT_DUAL_VFF;
        case VMAV_FR_VFQ:
            return VMAV_LT_DUAL_VFQ;
        case VMAV_FR_VFI:
            return VMAV_LT_DUAL_VFI;
        case VMAV_FR_UNKNOWN:
        default:
            return VMAV_LT_DUAL_VFF;
        }
    }
    /* French only. */
    if (has_fr && !has_en) {
        if (original_is_french) {
            return VMAV_LT_VO;
        }
        switch (french_variant) {
        case VMAV_FR_VFF:
            return VMAV_LT_VFF;
        case VMAV_FR_VFQ:
            return VMAV_LT_VFQ;
        case VMAV_FR_VFI:
            return VMAV_LT_VFI;
        case VMAV_FR_UNKNOWN:
        default:
            return VMAV_LT_FRENCH;
        }
    }
    /* No French track; if original language is French this is VFOST. */
    if (!has_fr && original_is_french) {
        return VMAV_LT_VOF;
    }
    return VMAV_LT_VO;
}

const char *vmav_naming_lang_tag_string(vmav_naming_lang_tag_t t) {
    switch (t) {
    case VMAV_LT_VO:
        return "VO";
    case VMAV_LT_VFF:
        return "VFF";
    case VMAV_LT_VOF:
        return "VOF";
    case VMAV_LT_VFQ:
        return "VFQ";
    case VMAV_LT_VFI:
        return "VFI";
    case VMAV_LT_MULTI:
        return "MULTi";
    case VMAV_LT_MULTI_VFF:
        return "MULTi.VFF";
    case VMAV_LT_MULTI_VFQ:
        return "MULTi.VFQ";
    case VMAV_LT_MULTI_VFI:
        return "MULTi.VFI";
    case VMAV_LT_MULTI_VF2:
        return "MULTi.VF2";
    case VMAV_LT_MULTI_VOF:
        return "MULTi.VOF";
    case VMAV_LT_DUAL_VFF:
        return "DUAL.VFF";
    case VMAV_LT_DUAL_VFQ:
        return "DUAL.VFQ";
    case VMAV_LT_DUAL_VFI:
        return "DUAL.VFI";
    case VMAV_LT_FRENCH:
        return "FRENCH";
    case VMAV_LT_TRUEFRENCH:
        return "TRUEFRENCH";
    case VMAV_LT_VOST:
        return "VOST";
    case VMAV_LT_FANSUB:
        return "FANSUB";
    }
    return "VO";
}

const char *vmav_naming_source_string(vmav_naming_source_t s) {
    switch (s) {
    case VMAV_SOURCE_BDRIP:
        return "BDRip";
    case VMAV_SOURCE_BLURAY:
        return "BluRay";
    case VMAV_SOURCE_REMUX:
        return "Remux";
    case VMAV_SOURCE_DVDRIP:
        return "DVDRip";
    case VMAV_SOURCE_DVDREMUX:
        return "DVDRemux";
    case VMAV_SOURCE_WEBRIP:
        return "WEBRip";
    case VMAV_SOURCE_WEBDL:
        return "WEB-DL";
    case VMAV_SOURCE_WEB:
        return "WEB";
    case VMAV_SOURCE_HDTV:
        return "HDTV";
    case VMAV_SOURCE_HDRIP:
        return "HDRip";
    case VMAV_SOURCE_TVRIP:
        return "TVRip";
    case VMAV_SOURCE_VHSRIP:
        return "VHSRip";
    case VMAV_SOURCE_UNKNOWN:
        return "WEB";
    }
    return "WEB";
}

static void sanitize_title(char *out, size_t out_size, const char *title) {
    if (out == NULL || out_size == 0) {
        return;
    }
    if (title == NULL) {
        out[0] = '\0';
        return;
    }
    size_t k = 0;
    bool last_dot = true;
    for (size_t i = 0; title[i] != '\0' && k + 1 < out_size; i++) {
        const unsigned char c = (unsigned char)title[i];
        if (isalnum(c)) {
            out[k++] = (char)c;
            last_dot = false;
        } else if (isspace(c) || c == ':' || c == '-' || c == '_' || c == ',' || c == '.') {
            if (!last_dot) {
                out[k++] = '.';
                last_dot = true;
            }
        }
        /* Other chars (punctuation, UTF-8 sequences) dropped. */
    }
    if (k > 0 && out[k - 1] == '.') {
        k--;
    }
    out[k] = '\0';
}

vmav_status_t vmav_naming_build(char *out,
                                size_t out_size,
                                const char *title,
                                int year,
                                vmav_naming_lang_tag_t lang_tag,
                                int video_height,
                                const vmav_hdr_info_t *hdr,
                                vmav_naming_source_t source,
                                const char *release_group) {
    if (out == NULL || out_size == 0 || title == NULL || year < 1900) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_naming_build: bad arg");
    }

    char clean_title[256];
    sanitize_title(clean_title, sizeof(clean_title), title);

    const char *res = "1080p";
    if (video_height >= 2160) {
        res = "2160p";
    } else if (video_height >= 1080) {
        res = "1080p";
    } else if (video_height >= 720) {
        res = "720p";
    } else if (video_height > 0) {
        res = "SD";
    }

    char hdr_tag[24];
    hdr_tag[0] = '\0';
    if (hdr != NULL) {
        if (hdr->has_dolby_vision) {
            snprintf(hdr_tag, sizeof(hdr_tag), ".DV");
        } else if (hdr->has_hdr10plus) {
            snprintf(hdr_tag, sizeof(hdr_tag), ".HDR10P");
        } else if (hdr->has_hdr10) {
            snprintf(hdr_tag, sizeof(hdr_tag), ".HDR10");
        }
    }

    const char *group =
        (release_group != NULL && release_group[0] != '\0') ? release_group : "vmav";

    const int wrote = snprintf(out,
                               out_size,
                               "%s.%d.%s.%s%s.%s-%s.mkv",
                               clean_title,
                               year,
                               res,
                               vmav_naming_lang_tag_string(lang_tag),
                               hdr_tag,
                               vmav_naming_source_string(source),
                               group);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_naming_build: output too long");
    }
    return VMAV_OK_STATUS;
}
