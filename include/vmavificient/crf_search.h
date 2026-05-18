/**
 * @file crf_search.h
 * @brief In-process CRF search using libSvtAv1Enc + libvmaf.
 *
 * Picks short samples from the source, encodes them at trial CRF values
 * with the active preset, and runs VMAF against the source to locate the
 * lowest CRF that meets the VMAF target. Replaces the legacy ab-av1
 * subprocess; no external binary needed at runtime.
 */

#ifndef CRF_SEARCH_H
#define CRF_SEARCH_H

#include "encode_preset.h"

typedef struct {
  int crf;            /**< Found CRF (1–63), or -1 on error. */
  int vmaf_target;    /**< Target VMAF passed in by the caller. */
  double vmaf_result; /**< Actual VMAF measured at the chosen CRF. */
  const char *error;  /**< Static string describing failure (NULL on success). */
} CrfSearchResult;

/**
 * @brief Locate the lowest CRF that meets the VMAF target.
 *
 * Decodes a handful of short reference samples from @p input_path (adaptive:
 * 1 sample first, escalates to 3 if the result is uncertain), encodes them
 * through libSvtAv1Enc directly with @p preset / @p film_grain, decodes the
 * AV1 output, and scores ref-vs-distorted with libvmaf (vmaf_v0.6.1,
 * harmonic-mean pooling). Performs a 4-trial linear-interp binary search.
 *
 * Probe encodes always use the same encoder configuration as the final
 * encode (apply_preset_to_config) so calibration carries over.
 *
 * @param input_path   Source video file.
 * @param vmaf_target  Target VMAF score (e.g., 93).
 * @param preset       Encoding preset (forwarded to the probe encodes).
 * @param film_grain   Film-grain level for the probe encodes.
 * @param vfilter      ffmpeg-style scale filter, NULL for none. Applied to
 *                     both reference and distorted so VMAF is measured at
 *                     the output resolution (e.g., HD companion encodes).
 * @return CrfSearchResult — crf >= 1 on success, -1 with error set on failure.
 */
CrfSearchResult run_crf_search(const char *input_path, int vmaf_target, const EncodePreset *preset,
                               int film_grain, const char *vfilter);

#endif /* CRF_SEARCH_H */
