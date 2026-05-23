#pragma once

#include "vmavificient/vmav_preset.h"
#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRF search.
 *
 * For each candidate CRF the search:
 *   1. Encodes short samples of the source through libSvtAv1Enc
 *      using the exact production config (encoder_svtav1 + preset).
 *   2. Decodes the AV1 packets back to YUV via lavc.
 *   3. Scores against the reference with libvmaf using harmonic-mean
 *      pooling.
 *
 * The search uses linear-slope interpolation to predict the next CRF
 * given the (CRF, VMAF) history. Sampling is adaptive: 1 sample for
 * speed, escalating to 3 if the 1-sample VMAF is clearly off-target.
 *
 * Pure-math API split:
 *   * vmav_crf_pick_samples        — sample start-frame picker
 *   * vmav_crf_next_guess          — slope-based next-CRF predictor
 *   * vmav_crf_binary_search       — generic search w/ scoring callback
 *     (this is what `vmav_crf_search` uses internally, exposed for
 *      testability with a mock scorer)
 *
 * Production API:
 *   * vmav_crf_search              — full encode+VMAF search */

/* Tuning. */
#define VMAV_CRF_SAMPLE_FRAMES 480
#define VMAV_CRF_INITIAL 30
#define VMAV_CRF_MAX_TRIALS 4
#define VMAV_CRF_CONVERGE_VMAF 0.5
#define VMAV_CRF_ESCALATE_VMAF 2.0
#define VMAV_CRF_DEFAULT_SLOPE 0.6
#define VMAV_CRF_MIN 18
#define VMAV_CRF_MAX 50

/* Pick `n` sample start-frame positions evenly spaced inside the
 * input. Skips the first/last 10% margin (avoids title cards + end
 * credits). Returns the actual number of samples selected (may be
 * fewer than `n` if the input is short). `out_starts` must have
 * room for at least `n` int64_t values. */
int vmav_crf_pick_samples(int64_t total_frames, int n, int frames_per_sample, int64_t *out_starts);

/* Predict the next CRF given the two most-recent (CRF, VMAF) points.
 * Uses a slope estimate from the two points; falls back to a
 * conservative default slope if the two points coincide. Result is
 * clamped to [crf_min, crf_max]. */
int vmav_crf_next_guess(
    int crf_a, double vmaf_a, int crf_b, double vmaf_b, int target, int crf_min, int crf_max);

/* Scoring callback. Implementations encode `frames` worth of samples
 * at `crf` and return a VMAF score in `*out_vmaf`. `userdata` is
 * threaded through opaquely. Return VMAV_ERR_* on failure; on success
 * VMAV_OK_STATUS with `*out_vmaf` set. */
typedef vmav_status_t (*vmav_crf_score_fn)(int crf, void *userdata, double *out_vmaf);

/* Generic binary search over CRF using slope interpolation. Returns
 * the best CRF found (highest CRF that meets target; otherwise the
 * closest to target). Up to VMAV_CRF_MAX_TRIALS evaluations of `fn`.
 *
 * Convergence: stops early when |vmaf - target| <= VMAV_CRF_CONVERGE_VMAF
 * or when the slope predicts a duplicate-of-history CRF. */
vmav_status_t vmav_crf_binary_search(int vmaf_target,
                                     int crf_min,
                                     int crf_max,
                                     int initial_crf,
                                     vmav_crf_score_fn fn,
                                     void *userdata,
                                     int *out_crf,
                                     double *out_vmaf);

/* Full encode+score search. `spec` describes the source + encoder
 * params; the function does all the heavy lifting (sample selection,
 * encode probe, AV1 decode, VMAF scoring, binary search). */
typedef struct {
    const char *input_path; /* absolute or relative path to the source */
    vmav_preset_t preset;
    int film_grain; /* 0..50, from vmav_svtav1_film_grain_from_score */
    int target_bitrate_kbps;
    int vmaf_target; /* target VMAF (0..100) */
    int crf_min;     /* clamp lower bound (default VMAV_CRF_MIN) */
    int crf_max;     /* clamp upper bound (default VMAV_CRF_MAX) */
} vmav_crf_search_spec_t;

typedef struct {
    int crf;        /* found CRF */
    double vmaf;    /* VMAF score at that CRF */
    int trials;     /* number of probe encodes performed */
    bool escalated; /* true if we ran the 3-sample refinement pass */
} vmav_crf_search_result_t;

vmav_status_t vmav_crf_search(const vmav_crf_search_spec_t *spec, vmav_crf_search_result_t *out);

#ifdef __cplusplus
}
#endif
