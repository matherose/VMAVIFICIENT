/**
 * @file media_ssimu2.h
 * @brief SSIMULACRA2 perceptual quality metric.
 *
 * Exposes two layers:
 *
 *   1. A raw FFI entry point into the embedded `vmav_ssimu2` Rust staticlib,
 *      which wraps the `ssimulacra2` and `yuvxyb` crates. This scores a
 *      single pair of YUV420P10 frames.
 *
 *   2. A higher-level C helper that decodes two video files in lockstep,
 *      converts each decoded frame to YUV420P10 (if needed), and aggregates
 *      per-frame SSIMULACRA2 scores into mean / median / p10 statistics.
 *
 * The integer values for color_primaries / transfer_characteristics /
 * matrix_coefficients follow the ITU-T H.273 numbering, which is identical
 * to libav's `AVColorPrimaries` / `AVColorTransferCharacteristic` /
 * `AVColorSpace` enums. You can pass `AVFrame::color_primaries` (etc.)
 * directly with no remapping.
 */
#ifndef VMAVIFICIENT_MEDIA_SSIMU2_H
#define VMAVIFICIENT_MEDIA_SSIMU2_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================== */
/*  Raw FFI into the Rust staticlib                                       */
/* ====================================================================== */

/** Sentinel returned on any failure. Real scores are in (-inf, 100]. */
#define VMAV_SSIMU2_ERROR (-1.0)

/**
 * Score one pair of YUV420P10 frames against each other.
 *
 * Pixel data is interpreted as little-endian uint16_t values in the low
 * 10 bits (the layout libav uses for AV_PIX_FMT_YUV420P10LE).
 *
 * @param ref_y, ref_u, ref_v       Reference frame plane base pointers.
 * @param dis_y, dis_u, dis_v       Distorted frame plane base pointers.
 * @param width                     Visible luma width in pixels (must be even).
 * @param height                    Visible luma height in pixels (must be even).
 * @param y_stride_bytes            Luma plane row stride, in bytes.
 * @param uv_stride_bytes           Chroma plane row stride, in bytes.
 * @param color_primaries           AVCOL_PRI_* value.
 * @param transfer_characteristics  AVCOL_TRC_* value.
 * @param matrix_coefficients       AVCOL_SPC_* value.
 * @param full_range                true for PC range, false for TV range.
 *
 * @return SSIMULACRA2 score in (-inf, 100], or VMAV_SSIMU2_ERROR on failure.
 */
double vmav_ssimu2_score_yuv420p10(const uint8_t *ref_y, const uint8_t *ref_u,
                                   const uint8_t *ref_v, const uint8_t *dis_y,
                                   const uint8_t *dis_u, const uint8_t *dis_v,
                                   uint32_t width, uint32_t height,
                                   uint32_t y_stride_bytes,
                                   uint32_t uv_stride_bytes,
                                   uint32_t color_primaries,
                                   uint32_t transfer_characteristics,
                                   uint32_t matrix_coefficients,
                                   bool full_range);

/* ====================================================================== */
/*  High-level file-pair scoring                                          */
/* ====================================================================== */

/**
 * Aggregated SSIMULACRA2 statistics for a pair of scored video files.
 *
 * On failure `error` is negative (an AVERROR code) and all other fields
 * are zeroed.
 */
typedef struct {
  double mean;        /**< Arithmetic mean across scored frames. */
  double median;      /**< 50th percentile. */
  double p10;         /**< 10th percentile — worst-case quality bucket. */
  double min;         /**< Lowest per-frame score. */
  double max;         /**< Highest per-frame score. */
  int frames_scored;  /**< Number of frame pairs actually scored. */
  int error;          /**< 0 on success, negative AVERROR on failure. */
} Ssimu2Result;

/**
 * @brief Score a distorted video against a reference, frame-by-frame.
 *
 * Both files are decoded with libav in lockstep. Each decoded frame is
 * converted to YUV420P10LE (via sws_scale if needed) and scored against
 * its counterpart. Color metadata (primaries / transfer / matrix / range)
 * is taken from the reference stream.
 *
 * The files must contain the same content at the same resolution and
 * frame count. This is the normal case during CRF search, where the
 * distorted file is a freshly-encoded version of a sample cut from the
 * source.
 *
 * @param ref_path      Path to the reference video file.
 * @param dis_path      Path to the distorted video file.
 * @param frame_stride  Score every Nth frame (1 = every frame, 5 = every
 *                      5th, etc.). Must be >= 1.
 *
 * @return A populated Ssimu2Result. Check `.error` before trusting stats.
 */
Ssimu2Result ssimu2_score_files(const char *ref_path, const char *dis_path,
                                int frame_stride);

#ifdef __cplusplus
}
#endif

#endif /* VMAVIFICIENT_MEDIA_SSIMU2_H */
