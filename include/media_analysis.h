/**
 * @file media_analysis.h
 * @brief Video grain / noise analysis via signalstats.
 */

#ifndef MEDIA_ANALYSIS_H
#define MEDIA_ANALYSIS_H

/** Number of sample windows extracted across the film. */
#define GRAIN_NUM_WINDOWS 4

/**
 * @brief Grain analysis result based on denoise-then-subtract noise estimation.
 */
typedef struct {
  int error;           /**< 0 on success, negative AVERROR on failure. */
  double avg_noise;    /**< Average noise level (YAVG of difference frames). */
  double grain_score;  /**< Normalized noise score in [0, 1] (max across windows). */
  int frames_analyzed; /**< Number of frames used for the computation. */

  /* Per-window detail. */
  double per_window_scores[GRAIN_NUM_WINDOWS]; /**< Individual Y scores per window. */
  int windows_succeeded;   /**< How many windows produced valid scores. */
  double grain_variance;   /**< Variance of per-window Y scores (high = inconsistent grain). */
  double chroma_grain_score; /**< Max of sCb/sCr averages across windows, 0..1. */

  /** Path to the grainiest window's grav1synth filmgrn1 table, or empty if no
   *  window succeeded. The caller owns the file on disk and should unlink it
   *  after consuming it (e.g., after the encode). */
  char grain_table_path[4096];
} GrainScore;

/**
 * @brief Estimate video grain level using noise estimation.
 *
 * Decodes sample frames spread across the video and runs them through a
 * split -> hqdn3d (spatial denoise) -> blend(difference) -> signalstats
 * filter chain.  The average brightness (YAVG) of the difference frames
 * directly measures the noise / grain level.
 *
 * Higher values indicate more visible grain / noise.
 *
 * @param path  Filesystem path to the media file.
 * @return A @ref GrainScore struct with the result.
 */
GrainScore get_grain_score(const char *path);

#endif /* MEDIA_ANALYSIS_H */
