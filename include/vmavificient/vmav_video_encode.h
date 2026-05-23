#pragma once

#include "vmavificient/vmav_preset.h"
#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Final video encode: source → decoded YUV → SVT-AV1-HDR → IVF file.
 * Uses the encoder_svtav1 wrapper internally; produces an IVF container
 * the final_mux step combines with audio + subtitle tracks into the
 * output .mkv.
 *
 * HDR pass-through (color primaries / transfer / matrix, mastering
 * display, content light level) is forwarded automatically from the
 * source codec parameters. Dolby Vision RPU sidecar attachment is
 * not yet wired here — that lands in a follow-up. */

typedef struct {
    const char *input_path;
    const char *output_path; /* .ivf path */
    vmav_preset_t preset;
    int film_grain;          /* 0..50; from vmav_svtav1_film_grain_from_score */
    int crf;                 /* CRF mode; > 0 to enable */
    int target_bitrate_kbps; /* VBR fallback; used when crf <= 0 */
} vmav_video_encode_spec_t;

typedef struct {
    char output_path[1024];
    int64_t frames_encoded;
    int64_t bytes_written;
    bool skipped; /* true if output existed and was reused */
} vmav_video_encode_result_t;

/* Run the encode. Skips the work if `output_path` already exists with
 * non-zero size. On failure any partial output is deleted. */
vmav_status_t vmav_video_encode(const vmav_video_encode_spec_t *spec,
                                vmav_video_encode_result_t *out);

#ifdef __cplusplus
}
#endif
