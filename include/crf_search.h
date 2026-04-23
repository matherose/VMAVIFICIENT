/**
 * @file crf_search.h
 * @brief Mean-VMAF-driven CRF search via interpolated binary search.
 *
 * Cuts short sample clips from a source, encodes each at adaptively chosen
 * CRF values (using the same film_grain and preset the real encode will use),
 * scores against the reference with VMAF, and converges on the lowest CRF
 * that meets the target mean VMAF. The measured per-sample bitrate at that
 * CRF is fed to the real encode as the VBR target — because probe and real
 * encoder settings match, the number is directly usable.
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
  int film_grain;             /**< Grain synthesis level (0–50) — applied to
                                   probes as well as the real encode. */
  double grain_score;         /**< Normalized grain score 0..1 from media_analysis. */
  double grain_variance;      /**< Per-window Y-score variance from media_analysis
                                   (high = inconsistent grain across the film). */
  QualityType quality;        /**< Content quality type (unused by the search
                                   but retained for encoder parameter symmetry). */

  double target_vmaf_mean;    /**< Desired mean VMAF. */
  int sample_count;           /**< Number of sample clips to cut (>=1). */
  int sample_duration;        /**< Seconds per sample clip (>=2). */
  int max_probes;             /**< Hard cap on probe encodes (default 6). */

  const char *workdir;        /**< Writable directory for sample/encoded files. */
} CrfSearchConfig;

/**
 * @brief Output of a CRF search run.
 */
typedef struct {
  int recommended_crf;         /**< CRF that meets target mean VMAF. */
  int measured_bitrate_kbps;   /**< Per-sample bitrate at recommended_crf.
                                    Fed directly to the final VBR encode. */
  double measured_vmaf_mean;   /**< Mean VMAF at recommended_crf (interpolated). */
  int probes_succeeded;        /**< Number of probe points that scored cleanly. */
  int error;                   /**< 0 on success, negative on failure. */
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
