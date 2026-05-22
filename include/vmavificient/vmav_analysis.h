#pragma once

#include "vmavificient/vmav_result.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Grain analysis result. `score` ∈ [0, 1] where 0 = clean (digital
 * intermediate, animation) and ~0.15+ = analog-film grain that
 * deserves --film-grain treatment. `variance` is the per-window
 * spread (currently 0 in the m5 baseline; refined in Phase 4 with
 * grav1synth multi-window sampling). */
typedef struct {
    double score;
    double variance;
} vmav_grain_score_t;

/* Estimate the grain level by decoding ~120 frames from the start of
 * the file and running `signalstats` to capture the mean Y-plane
 * deviation. The score is normalized to roughly [0, 1] using v1's
 * empirical calibration (signalstats.YDIF ~30 maps to score ~0.15).
 *
 * Returns the standard probe error codes; variance is always 0 in
 * this baseline impl. Phase 4 swaps in grav1synth-based multi-window
 * analysis with real variance. */
vmav_status_t vmav_grain_analyze(const char *path, vmav_grain_score_t *out);

#ifdef __cplusplus
}
#endif
