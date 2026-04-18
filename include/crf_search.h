/**
 * @file crf_search.h
 * @brief VMAF-driven CRF search via interpolated binary search.
 *
 * Cuts short sample clips from a source, encodes each at adaptively chosen
 * CRF values, scores against the reference with VMAF, and converges on the
 * lowest CRF that meets the target VMAF p10. XPSNR is computed on the chosen
 * probe purely as informational sanity-check output.
 *
 * The target metric is the 10th-percentile per-frame VMAF score (p10) — bounds
 * worst-case quality rather than the arithmetic mean.
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
  double grain_score;         /**< Normalized grain score 0..1 from media_analysis.
                                   Used to soften the VMAF target for grainy content
                                   (grain will be re-synthesized by film_grain). */
  QualityType quality;        /**< Content quality type — controls how grain_score
                                   influences the effective VMAF target. */

  double target_p10;          /**< Desired VMAF p10. */
  int sample_count;           /**< Number of sample clips to cut (>=1). */
  int sample_duration;        /**< Seconds per sample clip (>=2). */
  int max_probes;             /**< Hard cap on probe encodes (default 8). */

  const char *workdir;        /**< Writable directory for sample/encoded files. */
} CrfSearchConfig;

/**
 * @brief Output of a CRF search run.
 */
typedef struct {
  int recommended_crf;        /**< Final CRF suggestion. */
  double predicted_p10;       /**< Predicted VMAF p10 at recommended CRF. */
  double xpsnr_p10;           /**< XPSNR p10 (dB) on closest probe — informational. */
  int measured_bitrate_kbps;  /**< Predicted bitrate at recommended CRF. */
  int probes_succeeded;       /**< Number of probe points that scored cleanly. */
  int saturated;              /**< Non-zero if search hit CRF range edge. */
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
