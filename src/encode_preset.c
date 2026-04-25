/**
 * @file encode_preset.c
 * @brief SVT-AV1-HDR v4.0.1 encoding quality presets.
 */

#include "encode_preset.h"

/** Sentinel: parameter should use encoder default. */
#define UNSET (-1)

/* ====================================================================== */
/*  Grain-variance adaptation tunables                                    */
/* ====================================================================== */

/** Above this, per-window variance signals heterogeneous grain — grain
 *  synthesis bracket selection rounds up at boundaries. */
#define GRAIN_VARIANCE_HIGH_THRESHOLD 0.0040
/** Fraction of a bracket's own width near its upper edge that triggers the
 *  upward nudge. A score in (upper - width * proximity, upper] rounds up. */
#define GRAIN_BRACKET_BOUNDARY_PROXIMITY 0.10

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
        .key_frame_qindex_offset = -4,
        .key_frame_chroma_qindex_offset = -3,
        .luma_y_dc_qindex_offset = -2,
        .chroma_u_dc_qindex_offset = -1,
        .chroma_v_dc_qindex_offset = -1,
        .vbr_max_section_pct = 2000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -4,
        .key_frame_chroma_qindex_offset = -3,
        .luma_y_dc_qindex_offset = -2,
        .chroma_u_dc_qindex_offset = -1,
        .chroma_v_dc_qindex_offset = -1,
        .vbr_max_section_pct = 2000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -12,
        .key_frame_chroma_qindex_offset = -8,
        .luma_y_dc_qindex_offset = -6,
        .chroma_u_dc_qindex_offset = -4,
        .chroma_v_dc_qindex_offset = -4,
        .vbr_max_section_pct = 1500,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -12,
        .key_frame_chroma_qindex_offset = -8,
        .luma_y_dc_qindex_offset = -6,
        .chroma_u_dc_qindex_offset = -4,
        .chroma_v_dc_qindex_offset = -4,
        .vbr_max_section_pct = 1500,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -8,
        .key_frame_chroma_qindex_offset = -6,
        .luma_y_dc_qindex_offset = -3,
        .chroma_u_dc_qindex_offset = -2,
        .chroma_v_dc_qindex_offset = -2,
        .vbr_max_section_pct = 2500,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -8,
        .key_frame_chroma_qindex_offset = -6,
        .luma_y_dc_qindex_offset = -3,
        .chroma_u_dc_qindex_offset = -2,
        .chroma_v_dc_qindex_offset = -2,
        .vbr_max_section_pct = 2500,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -4,
        .key_frame_chroma_qindex_offset = -3,
        .luma_y_dc_qindex_offset = -2,
        .chroma_u_dc_qindex_offset = -1,
        .chroma_v_dc_qindex_offset = -1,
        .vbr_max_section_pct = 2000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -4,
        .key_frame_chroma_qindex_offset = -3,
        .luma_y_dc_qindex_offset = -2,
        .chroma_u_dc_qindex_offset = -1,
        .chroma_v_dc_qindex_offset = -1,
        .vbr_max_section_pct = 2000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -10,
        .key_frame_chroma_qindex_offset = -8,
        .luma_y_dc_qindex_offset = -3,
        .chroma_u_dc_qindex_offset = -2,
        .chroma_v_dc_qindex_offset = -2,
        .vbr_max_section_pct = 3000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -10,
        .key_frame_chroma_qindex_offset = -8,
        .luma_y_dc_qindex_offset = -3,
        .chroma_u_dc_qindex_offset = -2,
        .chroma_v_dc_qindex_offset = -2,
        .vbr_max_section_pct = 3000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -6,
        .key_frame_chroma_qindex_offset = -4,
        .luma_y_dc_qindex_offset = -2,
        .chroma_u_dc_qindex_offset = -1,
        .chroma_v_dc_qindex_offset = -1,
        .vbr_max_section_pct = 2000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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
        .key_frame_qindex_offset = -6,
        .key_frame_chroma_qindex_offset = -4,
        .luma_y_dc_qindex_offset = -2,
        .chroma_u_dc_qindex_offset = -1,
        .chroma_v_dc_qindex_offset = -1,
        .vbr_max_section_pct = 2000,
        .gop_constraint_rc = 1,
        .startup_mg_size = 3,
        .startup_qp_offset = -5,
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

/* Release-style flat table. The encoder handles grain structure via its
 * built-in denoise+synthesis path (film_grain_denoise_apply=1), so we do
 * NOT pay extra bits to preserve noise — we pick the right tier and let
 * the grain synthesizer rebuild texture on playback. */
#define GRAIN_THRESHOLD 0.08 /* grain_score above this → "grainy" tier */

int get_target_bitrate(int height, double grain_score) {
  int is_4k = (height >= 2160);
  int is_grainy = (grain_score >= GRAIN_THRESHOLD);

  if (is_4k)
    return is_grainy ? 4000 : 3500;
  return is_grainy ? 2500 : 2000;
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

/* Upper edges of each bracket in film_grain_analog and film_grain_digital.
 * Lower edge of bracket i is boundaries[i-1] (or 0 for bracket 0). Keep in
 * sync with the curves above. */
static const double ANALOG_BRACKETS[] = {0.04, 0.08, 0.12, 0.20, 1.00};
static const double DIGITAL_BRACKETS[] = {0.05, 0.10, 0.15, 0.25, 1.00};

/** If @p score sits within the top GRAIN_BRACKET_BOUNDARY_PROXIMITY of its
 *  bracket's width, return a score just past the upper edge so the curve
 *  dispatches into the next (grainier) bracket. Otherwise return @p score.
 *  Scores already above the last boundary are left alone (already at cap). */
static double nudge_to_higher_bracket(double score, const double *boundaries,
                                      int n) {
  for (int i = 0; i < n; i++) {
    if (score <= boundaries[i]) {
      double lower = (i == 0) ? 0.0 : boundaries[i - 1];
      double width = boundaries[i] - lower;
      double proximity = width * GRAIN_BRACKET_BOUNDARY_PROXIMITY;
      if (score > boundaries[i] - proximity)
        return boundaries[i] + 1e-6;
      return score;
    }
  }
  return score;
}

int get_film_grain_from_score(double grain_score, double grain_variance,
                              QualityType quality) {
  int nudge = grain_variance > GRAIN_VARIANCE_HIGH_THRESHOLD;
  switch (quality) {
  case QUALITY_SUPER35_ANALOG:
  case QUALITY_IMAX_ANALOG:
    if (nudge)
      grain_score = nudge_to_higher_bracket(
          grain_score, ANALOG_BRACKETS,
          sizeof(ANALOG_BRACKETS) / sizeof(ANALOG_BRACKETS[0]));
    return film_grain_analog(grain_score);

  case QUALITY_ANIMATION:
    /* Skip variance nudge: high variance on animation indicates scene cuts
     * and texture shifts, not real grain. */
    return film_grain_animation(grain_score);

  case QUALITY_LIVEACTION:
  case QUALITY_SUPER35_DIGITAL:
  case QUALITY_IMAX_DIGITAL:
  default:
    if (nudge)
      grain_score = nudge_to_higher_bracket(
          grain_score, DIGITAL_BRACKETS,
          sizeof(DIGITAL_BRACKETS) / sizeof(DIGITAL_BRACKETS[0]));
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
