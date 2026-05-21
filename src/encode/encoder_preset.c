#include "encoder_preset.h"

#include "config_defaults.h"
#include "util/str_utils.h"

#include <string.h>

/* Master preset table. Values mirror v1.5.0's encode_preset.c minus
 * the SVT-AV1-specific encoder fields; those move into Phase 4's
 * encoder_svtav1.c. Resolution-dependent VMAF + bitrate tiers stay
 * here so they're tunable from one place. */
static const vmav_preset_info_t PRESETS[VMAV_PRESET_COUNT_] = {
    {
        .preset = VMAV_PRESET_LIVE_ACTION,
        .cli_name = "live-action",
        .display_name = "Live action",
        .grain_mode = VMAV_GRAIN_SYNTHETIC,
        .vmaf_target_4k = VMAV_VMAF_LIVE_ACTION_4K,
        .vmaf_target_hd = VMAV_VMAF_LIVE_ACTION_HD,
        .bitrate_4k_grainy = VMAV_BITRATE_4K_GRAINY,
        .bitrate_4k_clean = VMAV_BITRATE_4K_CLEAN,
        .bitrate_4k_animation = 0,
        .bitrate_hd_grainy = VMAV_BITRATE_HD_GRAINY,
        .bitrate_hd_clean = VMAV_BITRATE_HD_CLEAN,
        .bitrate_hd_animation = 0,
    },
    {
        .preset = VMAV_PRESET_ANIMATION,
        .cli_name = "animation",
        .display_name = "Animation",
        .grain_mode = VMAV_GRAIN_SYNTHETIC,
        .vmaf_target_4k = VMAV_VMAF_ANIMATION_4K,
        .vmaf_target_hd = VMAV_VMAF_ANIMATION_HD,
        .bitrate_4k_grainy = 0,
        .bitrate_4k_clean = 0,
        .bitrate_4k_animation = VMAV_BITRATE_4K_ANIMATION,
        .bitrate_hd_grainy = 0,
        .bitrate_hd_clean = 0,
        .bitrate_hd_animation = VMAV_BITRATE_HD_ANIMATION,
    },
    {
        .preset = VMAV_PRESET_SUPER35_ANALOG,
        .cli_name = "super35-analog",
        .display_name = "Super 35 (analog film)",
        .grain_mode = VMAV_GRAIN_FILM,
        .vmaf_target_4k = VMAV_VMAF_SUPER35_4K,
        .vmaf_target_hd = VMAV_VMAF_SUPER35_HD,
        .bitrate_4k_grainy = VMAV_BITRATE_4K_GRAINY,
        .bitrate_4k_clean = VMAV_BITRATE_4K_CLEAN,
        .bitrate_4k_animation = 0,
        .bitrate_hd_grainy = VMAV_BITRATE_HD_GRAINY,
        .bitrate_hd_clean = VMAV_BITRATE_HD_CLEAN,
        .bitrate_hd_animation = 0,
    },
    {
        .preset = VMAV_PRESET_SUPER35_DIGITAL,
        .cli_name = "super35-digital",
        .display_name = "Super 35 (digital cinema)",
        .grain_mode = VMAV_GRAIN_SYNTHETIC,
        .vmaf_target_4k = VMAV_VMAF_SUPER35_4K,
        .vmaf_target_hd = VMAV_VMAF_SUPER35_HD,
        .bitrate_4k_grainy = VMAV_BITRATE_4K_GRAINY,
        .bitrate_4k_clean = VMAV_BITRATE_4K_CLEAN,
        .bitrate_4k_animation = 0,
        .bitrate_hd_grainy = VMAV_BITRATE_HD_GRAINY,
        .bitrate_hd_clean = VMAV_BITRATE_HD_CLEAN,
        .bitrate_hd_animation = 0,
    },
    {
        .preset = VMAV_PRESET_IMAX_ANALOG,
        .cli_name = "imax-analog",
        .display_name = "IMAX (analog film)",
        .grain_mode = VMAV_GRAIN_FILM,
        .vmaf_target_4k = VMAV_VMAF_IMAX_4K,
        .vmaf_target_hd = VMAV_VMAF_IMAX_HD,
        .bitrate_4k_grainy = VMAV_BITRATE_4K_GRAINY,
        .bitrate_4k_clean = VMAV_BITRATE_4K_CLEAN,
        .bitrate_4k_animation = 0,
        .bitrate_hd_grainy = VMAV_BITRATE_HD_GRAINY,
        .bitrate_hd_clean = VMAV_BITRATE_HD_CLEAN,
        .bitrate_hd_animation = 0,
    },
    {
        .preset = VMAV_PRESET_IMAX_DIGITAL,
        .cli_name = "imax-digital",
        .display_name = "IMAX (digital)",
        .grain_mode = VMAV_GRAIN_SYNTHETIC,
        .vmaf_target_4k = VMAV_VMAF_IMAX_4K,
        .vmaf_target_hd = VMAV_VMAF_IMAX_HD,
        .bitrate_4k_grainy = VMAV_BITRATE_4K_GRAINY,
        .bitrate_4k_clean = VMAV_BITRATE_4K_CLEAN,
        .bitrate_4k_animation = 0,
        .bitrate_hd_grainy = VMAV_BITRATE_HD_GRAINY,
        .bitrate_hd_clean = VMAV_BITRATE_HD_CLEAN,
        .bitrate_hd_animation = 0,
    },
};

const vmav_preset_info_t *vmav_preset_info(vmav_preset_t preset) {
    if ((int)preset < 0 || preset >= VMAV_PRESET_COUNT_) {
        return NULL;
    }
    return &PRESETS[preset];
}

const char *vmav_preset_name(vmav_preset_t preset) {
    const vmav_preset_info_t *info = vmav_preset_info(preset);
    return info != NULL ? info->cli_name : "unknown";
}

vmav_status_t vmav_preset_from_string(const char *name, vmav_preset_t *out) {
    if (name == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_preset_from_string: null arg");
    }
    char lc[64];
    vmav_str_to_lower(name, lc, sizeof(lc));
    for (size_t i = 0; i < VMAV_PRESET_COUNT_; i++) {
        if (strcmp(lc, PRESETS[i].cli_name) == 0) {
            *out = PRESETS[i].preset;
            return VMAV_OK_STATUS;
        }
    }
    /* Also accept v1's underscore form for backward compatibility. */
    for (size_t i = 0; i < VMAV_PRESET_COUNT_; i++) {
        char alt[64];
        size_t k = 0;
        for (const char *p = PRESETS[i].cli_name; *p != '\0' && k + 1 < sizeof(alt); p++, k++) {
            alt[k] = (*p == '-') ? '_' : *p;
        }
        alt[k] = '\0';
        if (strcmp(lc, alt) == 0) {
            *out = PRESETS[i].preset;
            return VMAV_OK_STATUS;
        }
    }
    return VMAV_ERR(VMAV_ERR_BAD_ARG, "unknown preset '%s'", name);
}

int vmav_preset_target_vmaf(vmav_preset_t preset, int video_height) {
    const vmav_preset_info_t *info = vmav_preset_info(preset);
    if (info == NULL) {
        return 0;
    }
    return (video_height >= VMAV_HEIGHT_4K_THRESHOLD) ? info->vmaf_target_4k : info->vmaf_target_hd;
}

int vmav_preset_target_bitrate(vmav_preset_t preset, int video_height, double grain_score) {
    const vmav_preset_info_t *info = vmav_preset_info(preset);
    if (info == NULL) {
        return 0;
    }
    const bool is_4k = (video_height >= VMAV_HEIGHT_4K_THRESHOLD);
    if (preset == VMAV_PRESET_ANIMATION) {
        return is_4k ? info->bitrate_4k_animation : info->bitrate_hd_animation;
    }
    const bool grainy = (grain_score >= VMAV_GRAIN_LOW_THRESHOLD);
    if (is_4k) {
        return grainy ? info->bitrate_4k_grainy : info->bitrate_4k_clean;
    }
    return grainy ? info->bitrate_hd_grainy : info->bitrate_hd_clean;
}
