/**
 * @file crf_search.h
 * @brief SSIMULACRA2-driven CRF search for quality-targeted AV1 encoding.
 *
 * Cuts short sample clips from a source, encodes each at several CRF values
 * using the project's real encode path, scores the encoded samples against
 * their references with SSIMULACRA2, and solves for the CRF that is
 * predicted to hit a target perceptual score.
 *
 * The target metric is the 10th-percentile per-frame score (p10), matching
 * the "Light" quality philosophy of bounding worst-case quality rather than
 * arithmetic mean.
 */
#ifndef VMAVIFICIENT_CRF_SEARCH_H
#define VMAVIFICIENT_CRF_SEARCH_H

#include "encode_preset.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inputs for a CRF search run.
 */
typedef struct {
  const char *input_path;     /**< Source media file. */
  const char *rpu_path;       /**< Dolby Vision RPU (NULL if none). */
  const EncodePreset *preset; /**< SVT-AV1 preset to probe with. */
  const MediaInfo *info;      /**< Source media info. */
  const CropInfo *crop;       /**< Crop info (NULL if none). */
  const HdrInfo *hdr;         /**< HDR info. */
  int film_grain;             /**< Grain synthesis level (0–50). */

  double target_p10;          /**< Desired SSIMULACRA2 p10 score. */
  int sample_count;           /**< Number of sample clips to cut (>=1). */
  int sample_duration;        /**< Seconds per sample clip (>=2). */
  int frame_stride;           /**< Score every Nth frame (>=1). */

  int crf_probes[4];          /**< CRF values to probe (0-terminated). */

  const char *workdir;        /**< Writable directory for sample/encoded files. */
} CrfSearchConfig;

/**
 * @brief Output of a CRF search run.
 */
typedef struct {
  int recommended_crf;        /**< Final CRF suggestion, clamped to [20,40]. */
  double predicted_p10;       /**< Predicted p10 at recommended CRF. */
  int measured_bitrate_kbps;  /**< Mean probe-sample bitrate at nearest probe,
                                   used to drive VBR on the real encode. */
  int probes_succeeded;       /**< Number of probe points that scored cleanly. */
  int error;                  /**< 0 on success, negative on failure. */
} CrfSearchResult;

/**
 * @brief Run a CRF search against @p cfg.
 *
 * Safe to call repeatedly with different configs. Intermediate files are
 * written under @p cfg->workdir and left in place on return (caller cleans up).
 */
CrfSearchResult crf_search_run(const CrfSearchConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* VMAVIFICIENT_CRF_SEARCH_H */
