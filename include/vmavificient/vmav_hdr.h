#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HDR capabilities of a video stream. Populated by vmav_hdr_probe;
 * consumers should ignore every field on a non-OK return. */
typedef struct {
    bool has_dolby_vision; /* AV_PKT_DATA_DOVI_CONF present on stream */
    int dv_profile;        /* -1 if no DV; otherwise from DOVI config */
    int dv_level;
    bool has_hdr10;     /* PQ (SMPTE ST 2084) transfer characteristic */
    bool has_hdr10plus; /* AV_FRAME_DATA_DYNAMIC_HDR_PLUS observed */
} vmav_hdr_info_t;

/* Open `path`, locate the best video stream, and report HDR caps.
 *
 *   - Dolby Vision is detected from codec-level side data (zero
 *     decoding, instant).
 *   - HDR10 is determined by color_trc == AVCOL_TRC_SMPTE2084.
 *   - HDR10+ probing decodes up to 30 frames looking for the
 *     dynamic-metadata side data; only done when has_hdr10 is true.
 *
 * Returns the same error codes as vmav_media_probe. */
vmav_status_t vmav_hdr_probe(const char *path, vmav_hdr_info_t *out);

#ifdef __cplusplus
}
#endif
