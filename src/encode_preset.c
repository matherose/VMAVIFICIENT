/**
 * @file encode_preset.c
 * @brief SVT-AV1-HDR v4.0.1 encoding quality presets.
 */

#include "encode_preset.h"

/** Sentinel: parameter should use encoder default. */
#define UNSET (-1)

/* ====================================================================== */
/*  Preset tables — index 0 = 4K (height >= 2160), index 1 = HD          */
/* ====================================================================== */

static const EncodePreset presets_liveaction[2] = {
    /* 4K */
    {
        .preset = 4,
        .keyint = 300,
        .tune = 0,
        .ac_bias = 2.5,
        .variance_boost = 2,
        .variance_octile = 6,
        .variance_curve = 0,
        .sharpness = 0,
        .luminance_bias = 7,
        .enable_tf = 1,
        .tf_strength = 1,
        .kf_tf_strength = 2,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 2,
        .noise_adaptive_filtering = 2,
        .enable_dlf = 2,
        .cdef_scaling = 15,
        .chroma_qm_min = 8,
        .chroma_qm_max = UNSET,
        .qp_scale_compress_strength = 2.0,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 5,
        .qm_max = 15,
        .spy_rd = 2,
        .undershoot_pct = 28,
        .overshoot_pct = 60,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
    /* HD */
    {
        .preset = 4,
        .keyint = 240,
        .tune = 0,
        .ac_bias = 2.0,
        .variance_boost = 2,
        .variance_octile = 6,
        .variance_curve = 0,
        .sharpness = 0,
        .luminance_bias = 7,
        .enable_tf = 1,
        .tf_strength = 1,
        .kf_tf_strength = 2,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 2,
        .noise_adaptive_filtering = 2,
        .enable_dlf = 2,
        .cdef_scaling = 15,
        .chroma_qm_min = 8,
        .chroma_qm_max = UNSET,
        .qp_scale_compress_strength = 2.0,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 5,
        .qm_max = 15,
        .spy_rd = 2,
        .undershoot_pct = 28,
        .overshoot_pct = 60,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
};

static const EncodePreset presets_animation[2] = {
    /* 4K */
    {
        .preset = 4,
        .keyint = 300,
        .tune = 0,
        .ac_bias = 3.0,
        .variance_boost = 1,
        .variance_octile = 4,
        .variance_curve = 1,
        .sharpness = 1,
        .luminance_bias = 0,
        .enable_tf = 0,
        .tf_strength = 0,
        .kf_tf_strength = 0,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 1,
        .noise_adaptive_filtering = 0,
        .enable_dlf = 2,
        .cdef_scaling = 12,
        .chroma_qm_min = 2,
        .chroma_qm_max = 8,
        .qp_scale_compress_strength = 3.0,
        .max_tx_size = 32,
        .hbd_mds = 2,
        .enable_overlays = UNSET,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 2,
        .qm_max = 12,
        .spy_rd = 1,
        .undershoot_pct = 25,
        .overshoot_pct = 50,
        .min_qp = 4,
        .max_qp = 50,
        .enable_mfmv = UNSET,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 0,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
    /* HD */
    {
        .preset = 4,
        .keyint = 240,
        .tune = 0,
        .ac_bias = 2.6,
        .variance_boost = 1,
        .variance_octile = 4,
        .variance_curve = 1,
        .sharpness = 1,
        .luminance_bias = 0,
        .enable_tf = 0,
        .tf_strength = 0,
        .kf_tf_strength = 0,
        .tx_bias = 0,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 1,
        .noise_adaptive_filtering = 0,
        .enable_dlf = 2,
        .cdef_scaling = 12,
        .chroma_qm_min = 2,
        .chroma_qm_max = 8,
        .qp_scale_compress_strength = 2.0,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = UNSET,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 2,
        .qm_max = 12,
        .spy_rd = 1,
        .undershoot_pct = 25,
        .overshoot_pct = 50,
        .min_qp = 4,
        .max_qp = 50,
        .enable_mfmv = UNSET,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 0,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
};

static const EncodePreset presets_super35_analog[2] = {
    /* 4K */
    {
        .preset = 4,
        .keyint = 300,
        .tune = 5,
        .ac_bias = 4.0,
        .variance_boost = 1,
        .variance_octile = 3,
        .variance_curve = 1,
        .sharpness = UNSET,
        .luminance_bias = 5,
        .enable_tf = 0,
        .tf_strength = 0,
        .kf_tf_strength = 0,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 1,
        .noise_norm_strength = 1,
        .noise_adaptive_filtering = 3,
        .enable_dlf = 1,
        .cdef_scaling = 10,
        .chroma_qm_min = 4,
        .chroma_qm_max = 12,
        .qp_scale_compress_strength = 1.5,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 4,
        .qm_max = 15,
        .spy_rd = 2,
        .undershoot_pct = 30,
        .overshoot_pct = 65,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 0,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
    /* HD */
    {
        .preset = 4,
        .keyint = 240,
        .tune = 5,
        .ac_bias = 3.0,
        .variance_boost = 1,
        .variance_octile = 3,
        .variance_curve = 1,
        .sharpness = 0,
        .luminance_bias = 5,
        .enable_tf = 0,
        .tf_strength = 0,
        .kf_tf_strength = 0,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 1,
        .noise_norm_strength = 1,
        .noise_adaptive_filtering = 3,
        .enable_dlf = 1,
        .cdef_scaling = 10,
        .chroma_qm_min = 4,
        .chroma_qm_max = 12,
        .qp_scale_compress_strength = 1.5,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 4,
        .qm_max = 15,
        .spy_rd = 2,
        .undershoot_pct = 30,
        .overshoot_pct = 65,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 0,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
};

static const EncodePreset presets_super35_digital[2] = {
    /* 4K */
    {
        .preset = 4,
        .keyint = 300,
        .tune = 0,
        .ac_bias = 3.5,
        .variance_boost = 2,
        .variance_octile = 4,
        .variance_curve = 0,
        .sharpness = 1,
        .luminance_bias = 8,
        .enable_tf = 1,
        .tf_strength = 1,
        .kf_tf_strength = 2,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 2,
        .noise_adaptive_filtering = 2,
        .enable_dlf = 2,
        .cdef_scaling = 15,
        .chroma_qm_min = 8,
        .chroma_qm_max = UNSET,
        .qp_scale_compress_strength = 2.0,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 5,
        .qm_max = 15,
        .spy_rd = 1,
        .undershoot_pct = 28,
        .overshoot_pct = 60,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
    /* HD */
    {
        .preset = 4,
        .keyint = 240,
        .tune = 0,
        .ac_bias = 2.5,
        .variance_boost = 2,
        .variance_octile = 4,
        .variance_curve = 0,
        .sharpness = 1,
        .luminance_bias = 8,
        .enable_tf = 1,
        .tf_strength = 1,
        .kf_tf_strength = 2,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 2,
        .noise_adaptive_filtering = 2,
        .enable_dlf = 2,
        .cdef_scaling = 15,
        .chroma_qm_min = 8,
        .chroma_qm_max = UNSET,
        .qp_scale_compress_strength = 2.0,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 5,
        .qm_max = 15,
        .spy_rd = 1,
        .undershoot_pct = 28,
        .overshoot_pct = 60,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
};

static const EncodePreset presets_imax_analog[2] = {
    /* 4K */
    {
        .preset = 4,
        .keyint = 300,
        .tune = 5,
        .ac_bias = 2.0,
        .variance_boost = 1,
        .variance_octile = 2,
        .variance_curve = 1,
        .sharpness = 0,
        .luminance_bias = 3,
        .enable_tf = 0,
        .tf_strength = 0,
        .kf_tf_strength = 0,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 1,
        .noise_norm_strength = 1,
        .noise_adaptive_filtering = 3,
        .enable_dlf = 1,
        .cdef_scaling = 8,
        .chroma_qm_min = 4,
        .chroma_qm_max = 12,
        .qp_scale_compress_strength = 1.5,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 4,
        .qm_max = 15,
        .spy_rd = 2,
        .undershoot_pct = 30,
        .overshoot_pct = 65,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 0,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
    /* HD */
    {
        .preset = 4,
        .keyint = 240,
        .tune = 5,
        .ac_bias = 1.8,
        .variance_boost = 1,
        .variance_octile = 2,
        .variance_curve = 1,
        .sharpness = 0,
        .luminance_bias = 3,
        .enable_tf = 0,
        .tf_strength = 0,
        .kf_tf_strength = 0,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 1,
        .noise_norm_strength = 1,
        .noise_adaptive_filtering = 3,
        .enable_dlf = 1,
        .cdef_scaling = 8,
        .chroma_qm_min = 4,
        .chroma_qm_max = 12,
        .qp_scale_compress_strength = 1.5,
        .max_tx_size = 32,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 4,
        .qm_max = 15,
        .spy_rd = 2,
        .undershoot_pct = 30,
        .overshoot_pct = 65,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 0,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
};

static const EncodePreset presets_imax_digital[2] = {
    /* 4K */
    {
        .preset = 4,
        .keyint = 300,
        .tune = 0,
        .ac_bias = 2.8,
        .variance_boost = 3,
        .variance_octile = 4,
        .variance_curve = 1,
        .sharpness = 2,
        .luminance_bias = 0,
        .enable_tf = 1,
        .tf_strength = 1,
        .kf_tf_strength = 2,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 3,
        .noise_adaptive_filtering = 2,
        .enable_dlf = 2,
        .cdef_scaling = 15,
        .chroma_qm_min = 8,
        .chroma_qm_max = UNSET,
        .qp_scale_compress_strength = 2.0,
        .max_tx_size = 64,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 5,
        .qm_max = 15,
        .spy_rd = 1,
        .undershoot_pct = 28,
        .overshoot_pct = 60,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
    /* HD */
    {
        .preset = 4,
        .keyint = 240,
        .tune = 0,
        .ac_bias = 2.3,
        .variance_boost = 3,
        .variance_octile = 4,
        .variance_curve = 1,
        .sharpness = 2,
        .luminance_bias = 0,
        .enable_tf = 1,
        .tf_strength = 1,
        .kf_tf_strength = 2,
        .tx_bias = 1,
        .sharp_tx = 1,
        .complex_hvs = 0,
        .noise_norm_strength = 3,
        .noise_adaptive_filtering = 2,
        .enable_dlf = 2,
        .cdef_scaling = 15,
        .chroma_qm_min = 8,
        .chroma_qm_max = UNSET,
        .qp_scale_compress_strength = 2.0,
        .max_tx_size = 64,
        .hbd_mds = 1,
        .enable_overlays = 0,
        .adaptive_film_grain = 1,
        .alt_lambda_factors = 1,
        .enable_qm = 1,
        .qm_min = 5,
        .qm_max = 15,
        .spy_rd = 1,
        .undershoot_pct = 28,
        .overshoot_pct = 60,
        .min_qp = 6,
        .max_qp = 56,
        .enable_mfmv = 1,
        .unrestricted_mv = 1,
        .irefresh_type = 1,
        .aq_mode = 2,
        .enable_restoration = 1,
        .recode_loop = 3,
        .look_ahead_distance = 120,
        .enable_dg = 1,
        .fast_decode = 0,
        .scd = 1,
        .temporal_layer_chroma_qindex_offset = 1,
    },
};

/* ====================================================================== */
/*  Lookup table                                                          */
/* ====================================================================== */

static const EncodePreset *const preset_tables[] = {
    [QUALITY_LIVEACTION] = presets_liveaction,
    [QUALITY_ANIMATION] = presets_animation,
    [QUALITY_SUPER35_ANALOG] = presets_super35_analog,
    [QUALITY_SUPER35_DIGITAL] = presets_super35_digital,
    [QUALITY_IMAX_ANALOG] = presets_imax_analog,
    [QUALITY_IMAX_DIGITAL] = presets_imax_digital,
};

const EncodePreset *get_encode_preset(QualityType quality, int video_height) {
  if (quality < 0 || quality > QUALITY_IMAX_DIGITAL)
    return &presets_liveaction[0];
  int idx = (video_height >= 2160) ? 0 : 1;
  return &preset_tables[quality][idx];
}

/* ====================================================================== */
/*  Target bitrate from resolution and grain score                        */
/* ====================================================================== */

int get_target_bitrate(int width, int height, double framerate,
                       double grain_score, int is_hdr, QualityType quality) {
  double fps = framerate > 0.0 ? framerate : 24.0;
  double pixels = (double)width * (double)height;

  double bpp;
  double grain_weight; /* How much grain_score adds to BPP. */
  double hdr_mult;     /* Multiplier for HDR overhead. */

  /* BPP calibrated against real CRF search results at preset 4 / VMAF p10 93.
   * Reference targets at 4K 24fps:
   *   Animation       ~2000 kbps  (0.010 bpp)
   *   Live-action     ~2500 kbps  (0.0125 bpp)
   *   Super35 digital ~2500 kbps  (0.0125 bpp)
   *   Super35 analog  ~3000 kbps  (0.015 bpp)
   *   IMAX digital    ~3000 kbps  (0.015 bpp)
   *   IMAX analog     ~4000 kbps  (0.020 bpp) */
  switch (quality) {
  case QUALITY_ANIMATION:
    bpp = 0.010;
    grain_weight = 0.0;   /* grain score is texture, not noise */
    hdr_mult = 1.10;
    break;

  case QUALITY_SUPER35_ANALOG:
    bpp = 0.015;
    grain_weight = 0.02;
    hdr_mult = 1.15;
    break;

  case QUALITY_IMAX_ANALOG:
    bpp = 0.020;
    grain_weight = 0.02;
    hdr_mult = 1.15;
    break;

  case QUALITY_IMAX_DIGITAL:
    bpp = 0.015;
    grain_weight = 0.01;
    hdr_mult = 1.15;
    break;

  case QUALITY_LIVEACTION:
  case QUALITY_SUPER35_DIGITAL:
  default:
    bpp = 0.0125;
    grain_weight = 0.01;
    hdr_mult = 1.15;
    break;
  }

  bpp += grain_score * grain_weight;

  if (is_hdr)
    bpp *= hdr_mult;

  double bitrate = pixels * fps * bpp / 1000.0; /* kbps */

  /* Sanity floor: never recommend less than 1000 kbps. */
  if (bitrate < 1000.0)
    bitrate = 1000.0;

  return (int)(bitrate + 0.5);
}

/* ====================================================================== */
/*  Dynamic film grain from grain score                                   */
/* ====================================================================== */

/**
 * Linear interpolation helper.
 */
static int lerp_grain(double score, double lo_score, double hi_score,
                      int lo_val, int hi_val) {
  double t = (score - lo_score) / (hi_score - lo_score);
  return lo_val + (int)(t * (hi_val - lo_val) + 0.5);
}

/**
 * Analog film (Super 35 / IMAX shot on film): the grain is real and
 * artistically intentional.  Map aggressively — the detector is measuring
 * actual film stock noise.  Reference: Blade Runner 2049 REMUX -> 0.108.
 */
static int film_grain_analog(double s) {
  if (s <= 0.04) return 0;
  if (s <= 0.08) return lerp_grain(s, 0.04, 0.08, 4, 10);
  if (s <= 0.12) return lerp_grain(s, 0.08, 0.12, 10, 20);
  if (s <= 0.20) return lerp_grain(s, 0.12, 0.20, 20, 30);
  if (s <= 1.00) return lerp_grain(s, 0.20, 1.00, 30, 40);
  return 40;
}

/**
 * Digital live-action / Super 35 digital / IMAX digital: minimal real grain,
 * detector mostly sees sensor noise and compression artifacts.
 * Moderate mapping — some synthesis helps mask banding, but don't overdo it.
 */
static int film_grain_digital(double s) {
  if (s <= 0.05) return 0;
  if (s <= 0.10) return lerp_grain(s, 0.05, 0.10, 2, 6);
  if (s <= 0.15) return lerp_grain(s, 0.10, 0.15, 6, 10);
  if (s <= 0.25) return lerp_grain(s, 0.15, 0.25, 10, 16);
  if (s <= 1.00) return lerp_grain(s, 0.25, 1.00, 16, 22);
  return 22;
}

/**
 * Animation (CGI, hand-drawn, anime): zero real grain.  What the detector
 * measures is texture detail, dithering, or compression artifacts — NOT noise.
 * Film grain synthesis would degrade the clean look.  Cap very low.
 */
static int film_grain_animation(double s) {
  if (s <= 0.10) return 0;
  if (s <= 0.25) return lerp_grain(s, 0.10, 0.25, 0, 4);
  return 4;
}

int get_film_grain_from_score(double grain_score, QualityType quality) {
  switch (quality) {
  case QUALITY_SUPER35_ANALOG:
  case QUALITY_IMAX_ANALOG:
    return film_grain_analog(grain_score);

  case QUALITY_ANIMATION:
    return film_grain_animation(grain_score);

  case QUALITY_LIVEACTION:
  case QUALITY_SUPER35_DIGITAL:
  case QUALITY_IMAX_DIGITAL:
  default:
    return film_grain_digital(grain_score);
  }
}

/* ====================================================================== */
/*  Display name                                                          */
/* ====================================================================== */

const char *quality_type_to_string(QualityType quality) {
  static const char *names[] = {
      [QUALITY_LIVEACTION] = "Live-Action",
      [QUALITY_ANIMATION] = "Animation",
      [QUALITY_SUPER35_ANALOG] = "Super 35 Analog",
      [QUALITY_SUPER35_DIGITAL] = "Super 35 Digital",
      [QUALITY_IMAX_ANALOG] = "IMAX Analog",
      [QUALITY_IMAX_DIGITAL] = "IMAX Digital",
  };
  if (quality < 0 || quality > QUALITY_IMAX_DIGITAL)
    return "Unknown";
  return names[quality];
}
