#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* High-level content presets. The full SVT-AV1-HDR-specific parameter
 * tables land in Phase 4 when the encoder is vendored; for now we
 * expose just the user-visible metadata used by the CLI, naming, and
 * bitrate/VMAF target lookups. */

typedef enum {
    VMAV_PRESET_LIVE_ACTION = 0,
    VMAV_PRESET_ANIMATION,
    VMAV_PRESET_SUPER35_ANALOG,
    VMAV_PRESET_SUPER35_DIGITAL,
    VMAV_PRESET_IMAX_ANALOG,
    VMAV_PRESET_IMAX_DIGITAL,
    VMAV_PRESET_COUNT_
} vmav_preset_t;

/* Selects how SVT-AV1 handles grain:
 *   - VMAV_GRAIN_SYNTHETIC: --noise, content-agnostic overlay. Used for
 *     digital sources (sensor noise, CGI) and animation (banding mask).
 *   - VMAV_GRAIN_FILM:      --film-grain + denoise=1, analyzes source
 *     grain, denoises, re-synthesizes. Used for analog film. */
typedef enum {
    VMAV_GRAIN_SYNTHETIC = 0,
    VMAV_GRAIN_FILM      = 1,
} vmav_grain_mode_t;

/* Stable preset metadata. Returned pointers reference static storage;
 * do not free. */
typedef struct {
    vmav_preset_t preset;
    const char *cli_name;     /* "live-action", "animation", ... — used with --preset */
    const char *display_name; /* "Live action", "Animation", ... */
    vmav_grain_mode_t grain_mode;
    int vmaf_target_4k;
    int vmaf_target_hd;
    int bitrate_4k_grainy;    /* kbps; selected when grain_score >= LOW threshold */
    int bitrate_4k_clean;
    int bitrate_4k_animation; /* only meaningful for VMAV_PRESET_ANIMATION */
    int bitrate_hd_grainy;
    int bitrate_hd_clean;
    int bitrate_hd_animation;
} vmav_preset_info_t;

/* Lookup. Returns NULL only if `preset` is out of range. */
const vmav_preset_info_t *vmav_preset_info(vmav_preset_t preset);

/* CLI-name parsing. Accepts the canonical kebab-case names listed in
 * vmav_preset_info_t::cli_name. Case-insensitive. Returns
 * VMAV_ERR_BAD_ARG on unknown input. */
vmav_status_t vmav_preset_from_string(const char *name, vmav_preset_t *out);

/* Canonical CLI name for `preset`, or "unknown" if out of range. */
const char *vmav_preset_name(vmav_preset_t preset);

/* VMAF target for the (preset, resolution) tuple. video_height >=
 * VMAV_HEIGHT_4K_THRESHOLD selects the 4K target. */
int vmav_preset_target_vmaf(vmav_preset_t preset, int video_height);

/* Target bitrate (kbps) for VBR fallback. `grain_score` selects
 * grainy vs clean tier (uses VMAV_GRAIN_LOW_THRESHOLD). Animation
 * ignores grain_score and uses its flat animation tier. */
int vmav_preset_target_bitrate(vmav_preset_t preset, int video_height, double grain_score);

#ifdef __cplusplus
}
#endif
