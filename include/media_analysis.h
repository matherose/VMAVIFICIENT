/**
 * @file media_analysis.h
 * @brief Video grain / noise analysis via signalstats.
 */

#ifndef MEDIA_ANALYSIS_H
#define MEDIA_ANALYSIS_H

/**
 * @brief Grain analysis result based on denoise-then-subtract noise estimation.
 */
typedef struct {
  int error;           /**< 0 on success, negative AVERROR on failure. */
  double avg_noise;    /**< Average noise level (YAVG of difference frames). */
  double grain_score;  /**< Normalized noise score in [0, 1]. */
  int frames_analyzed; /**< Number of frames used for the computation. */
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
