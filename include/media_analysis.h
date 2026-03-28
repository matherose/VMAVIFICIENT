/**
 * @file media_analysis.h
 * @brief Video grain / noise analysis via signalstats.
 */

#ifndef MEDIA_ANALYSIS_H
#define MEDIA_ANALYSIS_H

/**
 * @brief Grain analysis result based on signalstats TOUT and Y-Range.
 */
typedef struct {
  int error;           /**< 0 on success, negative AVERROR on failure. */
  double avg_tout;     /**< Average TOUT (temporal outlier) percentage. */
  double avg_yrange;   /**< Average luma range (YHIGH - YLOW). */
  double grain_score;  /**< Weighted composite score in [0, 1]. */
  int frames_analyzed; /**< Number of frames used for the computation. */
} GrainScore;

/**
 * @brief Estimate video grain level using signalstats metrics.
 *
 * Decodes up to 200 consecutive frames starting at ~30 %% of the video
 * duration and runs them through the FFmpeg @c signalstats filter to
 * collect TOUT (temporal outlier) and Y-Range data.
 *
 * The composite grain score is:
 * @code
 *   grain_score = 0.7 * (avg_tout / 100) + 0.3 * (1 - avg_yrange / max_y)
 * @endcode
 *
 * Higher values indicate more visible grain / noise.
 *
 * @param path  Filesystem path to the media file.
 * @return A @ref GrainScore struct with the result.
 */
GrainScore get_grain_score(const char *path);

#endif /* MEDIA_ANALYSIS_H */
