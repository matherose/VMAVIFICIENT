/**
 * @file encode_preset.h
 * @brief SVT-AV1-HDR v4.0.1 encoding quality presets.
 */

#ifndef ENCODE_PRESET_H
#define ENCODE_PRESET_H

/**
 * @brief Content quality type, selected via CLI flags.
 *
 * Determines the SVT-AV1 parameter set used for encoding.
 * Live-action is the default when no flag is specified.
 */
typedef enum {
  QUALITY_LIVEACTION,
  QUALITY_ANIMATION,
  QUALITY_SUPER35_ANALOG,
  QUALITY_SUPER35_DIGITAL,
  QUALITY_IMAX_ANALOG,
  QUALITY_IMAX_DIGITAL,
} QualityType;

/**
 * @brief SVT-AV1-HDR v4.0.1 encoding parameters.
 *
 * Film grain is NOT included here — it is computed dynamically from the
 * grain score via @ref get_film_grain_from_score().
 *
 * Fields set to -1 mean "use encoder default" (sentinel value).
 */
typedef struct {
  int preset;              /**< Encoder speed preset (always 4). */
  int keyint;              /**< Keyframe interval (300=4K, 240=HD). */
  int tune;                /**< Tune mode: 0=VQ, 1=PSNR, 5=Film Grain. */
  double ac_bias;          /**< AC bias / PsyRD (0.0–8.0). */
  int variance_boost;      /**< Variance boost strength (1–4). */
  int variance_octile;     /**< Variance boost octile (1–8). */
  int variance_curve;      /**< Variance boost curve (0–3, 3=PQ). */
  int sharpness;           /**< Sharpness (-7 to 7). */
  int luminance_bias;      /**< Luminance QP bias (0–100). */
  int enable_tf;           /**< Temporal filter: 0=off, 1=on, 2=adaptive. */
  int tf_strength;         /**< TF strength (0–4). */
  int kf_tf_strength;      /**< Keyframe TF strength (0–4). */
  int tx_bias;             /**< Transform bias (0–3). */
  int sharp_tx;            /**< Sharp transform opts (0–1). */
  int complex_hvs;         /**< Complex HVS model (0–1). */
  int noise_norm_strength; /**< Noise normalization (0–4, -1=default). */
  int noise_adaptive_filtering; /**< Noise-adaptive filtering (0–4). */
  int enable_dlf;               /**< Deblocking filter (0–2, 2=accurate). */
  int cdef_scaling;             /**< CDEF strength scaling (1–30). */
  int chroma_qm_min;            /**< Chroma QM min (0–15, -1=default). */
  int chroma_qm_max;            /**< Chroma QM max (0–15, -1=default). */
  double qp_scale_compress_strength; /**< QP scale compression (0.0–8.0). */
  int max_tx_size;                   /**< Max transform size: 32 or 64. */
  int hbd_mds;             /**< High bit-depth mode decisions (0–2). */
  int enable_overlays;     /**< Overlays (0–1, -1=default). */
  int adaptive_film_grain; /**< Resolution-adaptive grain (0–1). */
  int alt_lambda_factors;  /**< Alternative RDO lambdas (0–1). */
  int enable_qm;           /**< Quantization matrices (0–1). */
  int qm_min;              /**< QM min level (0–15, -1=default). */
  int qm_max;              /**< QM max level (0–15, -1=default). */
  int spy_rd;              /**< Perceptual RDO mode (0–4, -1=default). */
  int undershoot_pct;      /**< VBR undershoot limit (0–100, -1=default). */
  int overshoot_pct;       /**< VBR overshoot limit (0–100, -1=default). */
  int min_qp;              /**< Minimum QP allowed (0–63, -1=default). */
  int max_qp;              /**< Maximum QP allowed (0–63, -1=default). */
  int enable_mfmv;     /**< Motion field motion vectors (0–1, -1=default). */
  int unrestricted_mv; /**< Unrestricted motion vectors (0–1, -1=default). */
  int irefresh_type; /**< Intra refresh type (1=open GOP, 2=closed, -1=default).
                      */
  int aq_mode;       /**< Adaptive quantization mode (0–2, -1=default). */
  int enable_restoration;  /**< Restoration filter (0–1, -1=default). */
  int recode_loop;         /**< Recode loop level (0–4, -1=default). */
  int look_ahead_distance; /**< VBR look-ahead frames (0–120, -1=default). */
  int enable_dg;           /**< Dynamic GOP (0–1, -1=default). */
  int fast_decode; /**< Decoder-speed optimization level (0–2, -1=default). */
  int scd;         /**< Scene change detection (0–1, -1=default). */
  int temporal_layer_chroma_qindex_offset; /**< Chroma QIndex offset applied to
                                              all temporal layers (0–6,
                                              -1=default). */
} EncodePreset;

/**
 * @brief Get the encoding preset for a given quality type and resolution.
 *
 * Returns a pointer to static data — no allocation, no cleanup needed.
 * Resolution is determined by @p video_height: >= 2160 selects the 4K
 * preset, otherwise HD.
 *
 * @param quality       Content quality type.
 * @param video_height  Video height in pixels.
 * @return Pointer to the matching preset (never NULL).
 */
const EncodePreset *get_encode_preset(QualityType quality, int video_height);

/**
 * @brief Compute target bitrate (kbps) using a preset-aware BPP model.
 *
 * Base BPP varies by content type: animation compresses efficiently (0.010),
 * digital live-action needs moderate headroom (0.0125), and analog film
 * needs more bits to preserve grain texture (0.015–0.020).  Grain sensitivity
 * and HDR overhead are also preset-dependent.
 *
 * This is only used when CRF search fails or is skipped.
 *
 * @param width         Video width in pixels.
 * @param height        Video height in pixels.
 * @param framerate     Frame rate (fps); defaults to 24 if <= 0.
 * @param grain_score   Composite grain score in [0, 1].
 * @param is_hdr        Non-zero if HDR content.
 * @param quality       Content quality type.
 * @return Target bitrate in kbps.
 */
int get_target_bitrate(int width, int height, double framerate,
                       double grain_score, int is_hdr, QualityType quality);

/**
 * @brief Compute film grain synthesis level from a grain analysis score.
 *
 * The mapping is preset-aware: analog film presets trust the measured grain
 * and map it aggressively, digital/live-action presets are moderate, and
 * animation clamps to near-zero (the detector sees texture, not grain).
 *
 * @param grain_score  Composite grain score in [0, 1].
 * @param quality      Content quality type (affects the mapping curve).
 * @return Film grain synthesis level (0–50).
 */
int get_film_grain_from_score(double grain_score, QualityType quality);

/**
 * @brief Convert a quality type enum to its display string.
 */
const char *quality_type_to_string(QualityType quality);

#endif /* ENCODE_PRESET_H */
