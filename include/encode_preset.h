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

  /* Keyframe QP offsets — negative gives I-frames more bits. */
  int key_frame_qindex_offset;        /**< Luma QP offset for keyframes (-64 to 63, 0=default). */
  int key_frame_chroma_qindex_offset;  /**< Chroma QP offset for keyframes (-64 to 63, 0=default). */

  /* DC coefficient QP offsets — affect luminance/color smoothness. */
  int luma_y_dc_qindex_offset;   /**< Luma DC offset (-64 to 63, 0=default). */
  int chroma_u_dc_qindex_offset; /**< Cb DC offset (-64 to 63, 0=default). */
  int chroma_v_dc_qindex_offset; /**< Cr DC offset (-64 to 63, 0=default). */

  /* VBR rate shaping */
  int vbr_max_section_pct; /**< Max GOP bitrate as % of target (0–10000, -1=default 2000). */
  int gop_constraint_rc;   /**< Per-GOP rate matching (0–1, -1=default). */

  /* Startup mini-GOP tuning */
  int startup_mg_size;   /**< First mini-GOP size after KF: 0=off, 2/3/4 (0=default). */
  int startup_qp_offset; /**< QP offset for startup mini-GOP (-63 to 63, 0=default). */

  /* Grain mechanism selection (SVT-AV1-HDR 4.1.0+).
   *
   * use_noise = 1  → route synth strength to --noise (content-agnostic
   *                  overlay, no source denoising, zero encoding overhead).
   *                  Right for digital sources (sensor noise, CGI) and
   *                  animation (banding mask).
   *
   * use_noise = 0  → route synth strength to --film-grain + denoise=1
   *                  (analyzes source grain, denoises, re-synthesizes).
   *                  Right for analog film where the grain has spatial /
   *                  temporal structure worth preserving (Super 35 / IMAX
   *                  shot on film). */
  int use_noise;            /**< Grain mechanism: 1=--noise, 0=--film-grain. */
  int noise_size;           /**< Noise grain size: -1=auto (resolution-based),
                                 0–13=explicit. Only used when use_noise=1. */
  int noise_chroma_strength; /**< Chroma noise strength: -1=auto (60% of
                                  luma), 0=off, 1–200=explicit. */
  int noise_chroma_from_luma; /**< Apply chroma noise based on luma plane
                                   (0 default, 1 enables chroma noise on
                                   grayscale). */
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
 * @brief Compute target bitrate (kbps) from resolution, grain, and content
 *        type.
 *
 * Release-style flat targets — scene encoders pin bitrate by tier and grain,
 * not by perceptual score. Values calibrated for SVT-AV1-HDR @ preset 4 with
 * grain denoising (film_grain_denoise_apply=1) on:
 *
 *   4KLight (height >= 2160):
 *     noisy  source → 4000 kbps
 *     clean  source → 3500 kbps
 *     animation     → 3000 kbps   (clean - 500; no real grain to budget for)
 *
 *   HDLight (height < 2160):
 *     noisy  source → 2500 kbps
 *     clean  source → 2000 kbps
 *     animation     → 1500 kbps
 *
 * @param height      Video height in pixels (>= 2160 selects 4K).
 * @param grain_score Composite grain score in [0, 1]. Ignored for animation.
 * @param quality     Content quality type — animation gets a flat
 *                    clean-tier-minus-500 rate.
 * @return Target bitrate in kbps.
 */
int get_target_bitrate(int height, double grain_score, QualityType quality);

/**
 * @brief Compute film grain synthesis level from a grain analysis score.
 *
 * The mapping is preset-aware: analog film presets trust the measured grain
 * and map it aggressively, digital/live-action presets are moderate, and
 * animation clamps to near-zero (the detector sees texture, not grain).
 *
 * @param grain_score    Composite grain score in [0, 1].
 * @param grain_variance Per-window Y-score variance from media_analysis.
 *                       When high, rounds up at bracket boundaries toward
 *                       the grainier synthesis level (ignored for animation).
 * @param quality        Content quality type (affects the mapping curve).
 * @return Film grain synthesis level (0–50).
 */
int get_film_grain_from_score(double grain_score, double grain_variance,
                              QualityType quality);

/**
 * @brief Convert a quality type enum to its display string.
 */
const char *quality_type_to_string(QualityType quality);

#endif /* ENCODE_PRESET_H */
