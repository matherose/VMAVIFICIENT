/**
 * @file crf_search.h
 * @brief CRF search via ab-av1 crf-search subprocess.
 */

#ifndef CRF_SEARCH_H
#define CRF_SEARCH_H

#include "encode_preset.h"

typedef struct {
  int crf;            /**< Found CRF (1–63), or -1 on error. */
  int vmaf_target;    /**< The VMAF target used for this search. */
  int ab_av1_missing; /**< 1 if ab-av1 was not found in PATH. */
} CrfSearchResult;

/**
 * @brief Run ab-av1 crf-search to find the CRF meeting the VMAF target.
 *
 * Invokes `ab-av1 crf-search` as a subprocess, passing SVT-AV1 encoder
 * args derived from the preset and film_grain level so the probe uses the
 * same encoder configuration as the actual encode.  Blocks until complete.
 *
 * On success:  result.crf is in [1, 63].
 * On failure:  result.crf == -1; result.ab_av1_missing indicates cause.
 *
 * @param input_path   Path to the source video file.
 * @param vmaf_target  Target VMAF score (e.g., 93).
 * @param preset       Encoding preset (encoder args forwarded to ab-av1).
 * @param film_grain   Film grain level for the probe encode.
 * @return CrfSearchResult.
 */
CrfSearchResult run_crf_search(const char *input_path, int vmaf_target,
                               const EncodePreset *preset, int film_grain);

#endif /* CRF_SEARCH_H */
