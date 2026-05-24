/* SVT-AV1-HDR encoder wrapper.
 *
 * Maps a high-level vmav_preset_t + resolution onto the 50+ knob
 * EbSvtAv1EncConfiguration struct SVT-AV1-HDR expects, copies FFmpeg-
 * discovered HDR metadata into the encoder config, and exposes a
 * minimal encoder lifecycle (open/send/recv/release/close) the Phase 4
 * driver and CRF search consume.
 *
 * The preset parameter tables are direct ports of v1's
 * src/encode_preset/encode_preset.c values — calibrated for
 * SVT-AV1-HDR v4.1.0+ at preset 4. Bumping the encoder fork may
 * require re-tuning. */

#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_svtav1.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <EbSvtAv1Enc.h>
#include <EbSvtAv1Formats.h>

/* Sentinel for "use encoder default" in preset tables. */
#define UNSET (-1)

/* Grain analysis tuning shared with media_analysis. */
#define GRAIN_VARIANCE_HIGH_THRESHOLD 0.05
#define GRAIN_BRACKET_BOUNDARY_PROXIMITY 0.20

/* SVT-AV1-HDR v4.1.0+ knobs. Direct mirror of v1's EncodePreset. */
typedef struct {
    int preset;
    int keyint;
    int tune;
    double ac_bias;
    int variance_boost;
    int variance_octile;
    int variance_curve;
    int sharpness;
    int luminance_bias;
    int enable_tf;
    int tf_strength;
    int kf_tf_strength;
    int tx_bias;
    int sharp_tx;
    int complex_hvs;
    int noise_norm_strength;
    int noise_adaptive_filtering;
    int enable_dlf;
    int cdef_scaling;
    int chroma_qm_min;
    int chroma_qm_max;
    double qp_scale_compress_strength;
    int max_tx_size;
    int hbd_mds;
    int enable_overlays;
    int adaptive_film_grain;
    int alt_lambda_factors;
    int enable_qm;
    int qm_min;
    int qm_max;
    int spy_rd;
    int undershoot_pct;
    int overshoot_pct;
    int min_qp;
    int max_qp;
    int enable_mfmv;
    int unrestricted_mv;
    int irefresh_type;
    int aq_mode;
    int enable_restoration;
    int recode_loop;
    int look_ahead_distance;
    int enable_dg;
    int fast_decode;
    int scd;
    int temporal_layer_chroma_qindex_offset;
    int key_frame_qindex_offset;
    int key_frame_chroma_qindex_offset;
    int luma_y_dc_qindex_offset;
    int chroma_u_dc_qindex_offset;
    int chroma_v_dc_qindex_offset;
    int vbr_max_section_pct;
    int gop_constraint_rc;
    int startup_mg_size;
    int startup_qp_offset;
    int use_noise;
    int noise_size;
    int noise_chroma_strength;
    int noise_chroma_from_luma;
} vmav_svtav1_params_t;

static const vmav_svtav1_params_t presets_liveaction[2] = {
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
    },
};

static const vmav_svtav1_params_t presets_animation[2] = {
    /* 4K */
    {
        .preset = 4,
        .keyint = 300,
        .tune = 0,
        .ac_bias = 1.0,
        .variance_boost = 2,
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
        .cdef_scaling = 8,
        .chroma_qm_min = 2,
        .chroma_qm_max = 8,
        .qp_scale_compress_strength = 1.0,
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
        .max_qp = 45,
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
    },
    /* HD */
    {
        .preset = 4,
        .keyint = 240,
        .tune = 0,
        .ac_bias = 1.0,
        .variance_boost = 2,
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
        .qp_scale_compress_strength = 1.0,
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
    },
};

static const vmav_svtav1_params_t presets_super35_analog[2] = {
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
        .use_noise = 0,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
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
        .use_noise = 0,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
    },
};

static const vmav_svtav1_params_t presets_super35_digital[2] = {
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
    },
};

static const vmav_svtav1_params_t presets_imax_analog[2] = {
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
        .use_noise = 0,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
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
        .use_noise = 0,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
    },
};

static const vmav_svtav1_params_t presets_imax_digital[2] = {
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
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
        .use_noise = 1,
        .noise_size = -1,
        .noise_chroma_strength = -1,
        .noise_chroma_from_luma = 0,
    },
};

/* ====================================================================== */
/*  Lookup table                                                          */
/* ====================================================================== */

/* Per-preset lookup table. Indexed by vmav_preset_t. */
static const vmav_svtav1_params_t *const preset_tables[VMAV_PRESET_COUNT_] = {
    [VMAV_PRESET_LIVE_ACTION] = presets_liveaction,
    [VMAV_PRESET_ANIMATION] = presets_animation,
    [VMAV_PRESET_SUPER35_ANALOG] = presets_super35_analog,
    [VMAV_PRESET_SUPER35_DIGITAL] = presets_super35_digital,
    [VMAV_PRESET_IMAX_ANALOG] = presets_imax_analog,
    [VMAV_PRESET_IMAX_DIGITAL] = presets_imax_digital,
};

/* Lookup preset params by (preset, video_height). Returns the 4K
 * params for height >= 2160, HD otherwise. Falls back to live-action
 * 4K on out-of-range input. */
static const vmav_svtav1_params_t *params_for_preset(vmav_preset_t preset, int video_height) {
    if (preset < 0 || preset >= VMAV_PRESET_COUNT_) {
        return &presets_liveaction[0];
    }
    const int idx = (video_height >= 2160) ? 0 : 1;
    return &preset_tables[preset][idx];
}

/* ====================================================================== */
/*  Film grain mapping                                                    */
/* ====================================================================== */

static int lerp_grain(double score, double lo_score, double hi_score, int lo_val, int hi_val) {
    const double t = (score - lo_score) / (hi_score - lo_score);
    return lo_val + (int)(t * (hi_val - lo_val) + 0.5);
}

static int film_grain_analog(double s) {
    if (s <= 0.04) {
        return 0;
    }
    if (s <= 0.08) {
        return lerp_grain(s, 0.04, 0.08, 3, 6);
    }
    if (s <= 0.12) {
        return lerp_grain(s, 0.08, 0.12, 6, 10);
    }
    if (s <= 0.20) {
        return lerp_grain(s, 0.12, 0.20, 10, 13);
    }
    if (s <= 1.00) {
        return lerp_grain(s, 0.20, 1.00, 13, 15);
    }
    return 15;
}

static int film_grain_digital(double s) {
    if (s <= 0.05) {
        return 0;
    }
    if (s <= 0.10) {
        return lerp_grain(s, 0.05, 0.10, 2, 5);
    }
    if (s <= 0.15) {
        return lerp_grain(s, 0.10, 0.15, 5, 7);
    }
    if (s <= 0.25) {
        return lerp_grain(s, 0.15, 0.25, 7, 9);
    }
    if (s <= 1.00) {
        return lerp_grain(s, 0.25, 1.00, 9, 10);
    }
    return 10;
}

static int film_grain_animation(double s) {
    if (s <= 0.10) {
        return 0;
    }
    if (s <= 0.25) {
        return lerp_grain(s, 0.10, 0.25, 0, 3);
    }
    return 3;
}

/* Upper edges of each bracket in film_grain_analog/digital. Lower edge of
 * bracket i is boundaries[i-1] (or 0 for bracket 0). Keep in sync. */
static const double ANALOG_BRACKETS[] = {0.04, 0.08, 0.12, 0.20, 1.00};
static const double DIGITAL_BRACKETS[] = {0.05, 0.10, 0.15, 0.25, 1.00};

/* If `score` sits within the top GRAIN_BRACKET_BOUNDARY_PROXIMITY of its
 * bracket's width, return a score just past the upper edge so the curve
 * dispatches into the next (grainier) bracket. */
static double nudge_to_higher_bracket(double score, const double *boundaries, int n) {
    for (int i = 0; i < n; i++) {
        if (score <= boundaries[i]) {
            const double lower = (i == 0) ? 0.0 : boundaries[i - 1];
            const double width = boundaries[i] - lower;
            const double proximity = width * GRAIN_BRACKET_BOUNDARY_PROXIMITY;
            if (score > boundaries[i] - proximity) {
                return boundaries[i] + 1e-6;
            }
            return score;
        }
    }
    return score;
}

int vmav_svtav1_film_grain_from_score(double grain_score,
                                      double grain_variance,
                                      vmav_preset_t preset) {
    const bool nudge = grain_variance > GRAIN_VARIANCE_HIGH_THRESHOLD;
    switch (preset) {
    case VMAV_PRESET_SUPER35_ANALOG:
    case VMAV_PRESET_IMAX_ANALOG:
        if (nudge) {
            grain_score = nudge_to_higher_bracket(
                grain_score, ANALOG_BRACKETS, sizeof(ANALOG_BRACKETS) / sizeof(ANALOG_BRACKETS[0]));
        }
        return film_grain_analog(grain_score);
    case VMAV_PRESET_ANIMATION:
        /* Skip variance nudge: high variance on animation indicates scene
         * cuts and texture shifts, not real grain. */
        return film_grain_animation(grain_score);
    case VMAV_PRESET_LIVE_ACTION:
    case VMAV_PRESET_SUPER35_DIGITAL:
    case VMAV_PRESET_IMAX_DIGITAL:
    default:
        if (nudge) {
            grain_score =
                nudge_to_higher_bracket(grain_score,
                                        DIGITAL_BRACKETS,
                                        sizeof(DIGITAL_BRACKETS) / sizeof(DIGITAL_BRACKETS[0]));
        }
        return film_grain_digital(grain_score);
    }
}

/* ====================================================================== */
/*  Apply preset params to SVT-AV1 config struct                          */
/*  Ported verbatim from v1 src/video_encode/encoder_config.c.            */
/* ====================================================================== */

static void apply_params_to_svtav1_cfg(EbSvtAv1EncConfiguration *cfg,
                                       const vmav_svtav1_params_t *p,
                                       int film_grain,
                                       int target_bitrate_kbps,
                                       int crf) {
    cfg->enc_mode = (int8_t)p->preset;
    cfg->intra_period_length = p->keyint;
    cfg->tune = (uint8_t)p->tune;
    if (crf > 0) {
        cfg->rate_control_mode = SVT_AV1_RC_MODE_CQP_OR_CRF;
        cfg->qp = (uint32_t)crf;
    } else {
        cfg->rate_control_mode = SVT_AV1_RC_MODE_VBR;
        cfg->target_bit_rate = (uint32_t)target_bitrate_kbps * 1000;
    }

    cfg->ac_bias = p->ac_bias;
    cfg->enable_variance_boost = true;
    cfg->variance_boost_strength = (uint8_t)p->variance_boost;
    cfg->variance_octile = (uint8_t)p->variance_octile;
    cfg->variance_boost_curve = (uint8_t)p->variance_curve;
    cfg->sharpness = (int8_t)p->sharpness;
    cfg->luminance_qp_bias = (uint8_t)p->luminance_bias;
    cfg->enable_tf = (uint8_t)p->enable_tf;
    cfg->tf_strength = (uint8_t)p->tf_strength;
    cfg->kf_tf_strength = (uint8_t)p->kf_tf_strength;
    cfg->tx_bias = (uint8_t)p->tx_bias;
    cfg->sharp_tx = (uint8_t)p->sharp_tx;
    cfg->complex_hvs = (uint8_t)p->complex_hvs;
    cfg->noise_adaptive_filtering = (uint8_t)p->noise_adaptive_filtering;
    cfg->enable_dlf_flag = (uint8_t)p->enable_dlf;
    cfg->cdef_scaling = (uint8_t)p->cdef_scaling;
    cfg->qp_scale_compress_strength = p->qp_scale_compress_strength;
    cfg->max_tx_size = (uint8_t)p->max_tx_size;
    cfg->hbd_mds = (uint8_t)p->hbd_mds;
    cfg->adaptive_film_grain = (p->adaptive_film_grain == 1);
    cfg->alt_lambda_factors = (p->alt_lambda_factors == 1);

    if (p->noise_norm_strength != UNSET) {
        cfg->noise_norm_strength = (uint8_t)p->noise_norm_strength;
    }
    if (p->chroma_qm_min != UNSET) {
        cfg->min_chroma_qm_level = (uint8_t)p->chroma_qm_min;
    }
    if (p->chroma_qm_max != UNSET) {
        cfg->max_chroma_qm_level = (uint8_t)p->chroma_qm_max;
    }
    if (p->enable_overlays != UNSET) {
        cfg->enable_overlays = (p->enable_overlays == 1);
    }

    if (p->enable_qm >= 0) {
        cfg->enable_qm = (p->enable_qm == 1);
    }
    if (p->qm_min != UNSET) {
        cfg->min_qm_level = (uint8_t)p->qm_min;
    }
    if (p->qm_max != UNSET) {
        cfg->max_qm_level = (uint8_t)p->qm_max;
    }
    if (p->undershoot_pct != UNSET) {
        cfg->under_shoot_pct = (uint32_t)p->undershoot_pct;
    }
    if (p->overshoot_pct != UNSET) {
        cfg->over_shoot_pct = (uint32_t)p->overshoot_pct;
    }
    if (p->min_qp != UNSET) {
        cfg->min_qp_allowed = (uint32_t)p->min_qp;
    }
    if (p->max_qp != UNSET) {
        cfg->max_qp_allowed = (uint32_t)p->max_qp;
    }
    if (p->enable_mfmv != UNSET) {
        cfg->enable_mfmv = (p->enable_mfmv == 1);
    }
    if (p->irefresh_type != UNSET) {
        cfg->intra_refresh_type = (int8_t)p->irefresh_type;
    }
    if (p->aq_mode != UNSET) {
        cfg->aq_mode = (uint8_t)p->aq_mode;
    }
    if (p->enable_restoration != UNSET) {
        cfg->enable_restoration_filtering = (p->enable_restoration == 1);
    }
    if (p->recode_loop != UNSET) {
        cfg->recode_loop = (uint32_t)p->recode_loop;
    }
    if (p->look_ahead_distance != UNSET) {
        cfg->look_ahead_distance = (uint32_t)p->look_ahead_distance;
    }
    if (p->enable_dg != UNSET) {
        cfg->enable_dg = (p->enable_dg == 1);
    }
    if (p->fast_decode != UNSET) {
        cfg->fast_decode = (uint8_t)p->fast_decode;
    }
    if (p->scd != UNSET) {
        cfg->scene_change_detection = (uint32_t)(p->scd == 1);
    }
    if (p->temporal_layer_chroma_qindex_offset != UNSET) {
        for (int i = 0; i < EB_MAX_TEMPORAL_LAYERS; i++) {
            cfg->chroma_qindex_offsets[i] = (int32_t)p->temporal_layer_chroma_qindex_offset;
        }
    }

    /* Keyframe QP offsets — mode 2 = apply on top of CRF-derived QP. */
    if (p->key_frame_qindex_offset != 0 || p->key_frame_chroma_qindex_offset != 0) {
        cfg->use_fixed_qindex_offsets = 2;
        cfg->key_frame_qindex_offset = (int32_t)p->key_frame_qindex_offset;
        cfg->key_frame_chroma_qindex_offset = (int32_t)p->key_frame_chroma_qindex_offset;
    }

    if (crf <= 0) {
        if (p->vbr_max_section_pct > 0) {
            cfg->vbr_max_section_pct = (uint32_t)p->vbr_max_section_pct;
        }
    } else {
        if (p->startup_mg_size > 0) {
            cfg->startup_mg_size = (uint8_t)p->startup_mg_size;
        }
        if (p->startup_qp_offset != 0) {
            cfg->startup_qp_offset = (int8_t)p->startup_qp_offset;
        }
    }

    if (p->use_noise) {
        cfg->noise_strength = (uint8_t)film_grain;
        cfg->noise_strength_chroma = (int32_t)p->noise_chroma_strength;
        cfg->noise_chroma_from_luma = (uint8_t)p->noise_chroma_from_luma;
        cfg->noise_size = (int8_t)p->noise_size;
        cfg->film_grain_denoise_strength = 0;
        cfg->film_grain_denoise_apply = 0;
    } else {
        cfg->film_grain_denoise_strength = (uint32_t)film_grain;
        cfg->film_grain_denoise_apply = (film_grain > 0) ? 1 : 0;
        cfg->noise_strength = 0;
    }
}

static void apply_color_to_svtav1_cfg(EbSvtAv1EncConfiguration *cfg, const vmav_svtav1_spec_t *s) {
    cfg->color_primaries = (EbColorPrimaries)s->color_primaries;
    cfg->transfer_characteristics = (EbTransferCharacteristics)s->color_trc;
    cfg->matrix_coefficients = (EbMatrixCoefficients)s->color_space;
    /* AVCOL_RANGE_JPEG == 2 → EB_CR_FULL_RANGE, else studio. */
    cfg->color_range = (s->color_range == 2) ? EB_CR_FULL_RANGE : EB_CR_STUDIO_RANGE;
}

static void apply_hdr10_to_svtav1_cfg(EbSvtAv1EncConfiguration *cfg, const vmav_svtav1_spec_t *s) {
    if (s->has_mastering) {
        cfg->mastering_display.r.x = (uint16_t)(s->mastering_red_x * 50000 + 0.5);
        cfg->mastering_display.r.y = (uint16_t)(s->mastering_red_y * 50000 + 0.5);
        cfg->mastering_display.g.x = (uint16_t)(s->mastering_green_x * 50000 + 0.5);
        cfg->mastering_display.g.y = (uint16_t)(s->mastering_green_y * 50000 + 0.5);
        cfg->mastering_display.b.x = (uint16_t)(s->mastering_blue_x * 50000 + 0.5);
        cfg->mastering_display.b.y = (uint16_t)(s->mastering_blue_y * 50000 + 0.5);
        cfg->mastering_display.white_point.x = (uint16_t)(s->mastering_white_x * 50000 + 0.5);
        cfg->mastering_display.white_point.y = (uint16_t)(s->mastering_white_y * 50000 + 0.5);
        cfg->mastering_display.max_luma = (uint32_t)(s->mastering_max_luma * 10000 + 0.5);
        cfg->mastering_display.min_luma = (uint32_t)(s->mastering_min_luma * 10000 + 0.5);
    }
    if (s->has_cll) {
        cfg->content_light_level.max_cll = s->cll_max_cll;
        cfg->content_light_level.max_fall = s->cll_max_fall;
    }
}

/* ====================================================================== */
/*  Encoder lifecycle                                                     */
/* ====================================================================== */

struct vmav_svtav1_encoder {
    EbComponentType *svt_handle;
    EbSvtAv1EncConfiguration cfg;
    /* Reusable input picture buffer. SVT-AV1 expects an EbBufferHeaderType
     * with EbSvtIOFormat payload; we keep one and refill the plane pointers
     * per send_picture call. */
    EbBufferHeaderType input_hdr;
    EbSvtIOFormat input_pic;
    /* Last packet handed out via recv; released back to SVT on next call
     * or close. */
    EbBufferHeaderType *outstanding_pkt;
    int bit_depth;
};

vmav_status_t vmav_svtav1_encoder_open(const vmav_svtav1_spec_t *spec,
                                       vmav_svtav1_encoder_t **out) {
    if (spec == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_svtav1_encoder_open: null arg");
    }
    *out = NULL;
    if (spec->width <= 0 || spec->height <= 0) {
        return VMAV_ERR(
            VMAV_ERR_BAD_ARG, "encoder_open: bad dims %dx%d", spec->width, spec->height);
    }
    if (spec->bit_depth != 8 && spec->bit_depth != 10) {
        return VMAV_ERR(
            VMAV_ERR_BAD_ARG, "encoder_open: bit_depth %d (need 8 or 10)", spec->bit_depth);
    }

    vmav_svtav1_encoder_t *enc = calloc(1, sizeof(*enc));
    if (enc == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "calloc encoder");
    }
    enc->bit_depth = spec->bit_depth;

    EbErrorType rc = svt_av1_enc_init_handle(&enc->svt_handle, &enc->cfg);
    if (rc != EB_ErrorNone || enc->svt_handle == NULL) {
        free(enc);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "svt_av1_enc_init_handle: %d", rc);
    }

    /* Fill defaults from spec. */
    enc->cfg.source_width = (uint32_t)spec->width;
    enc->cfg.source_height = (uint32_t)spec->height;
    enc->cfg.encoder_bit_depth = (uint32_t)spec->bit_depth;
    enc->cfg.frame_rate_numerator = (uint32_t)spec->fps_num;
    enc->cfg.frame_rate_denominator = (uint32_t)spec->fps_den;
    enc->cfg.frame_scale_evts.evt_num = 0;
    enc->cfg.level_of_parallelism = 0;

    /* Preset/CRF/HDR/grain knobs. */
    const vmav_svtav1_params_t *params = params_for_preset(spec->preset, spec->height);
    apply_params_to_svtav1_cfg(
        &enc->cfg, params, spec->film_grain, spec->target_bitrate_kbps, spec->crf);
    apply_color_to_svtav1_cfg(&enc->cfg, spec);
    apply_hdr10_to_svtav1_cfg(&enc->cfg, spec);

    rc = svt_av1_enc_set_parameter(enc->svt_handle, &enc->cfg);
    if (rc != EB_ErrorNone) {
        svt_av1_enc_deinit_handle(enc->svt_handle);
        free(enc);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "svt_av1_enc_set_parameter: %d", rc);
    }
    rc = svt_av1_enc_init(enc->svt_handle);
    if (rc != EB_ErrorNone) {
        svt_av1_enc_deinit_handle(enc->svt_handle);
        free(enc);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "svt_av1_enc_init: %d", rc);
    }

    enc->input_hdr.size = sizeof(EbBufferHeaderType);
    enc->input_hdr.p_buffer = (uint8_t *)&enc->input_pic;
    enc->input_hdr.pic_type = EB_AV1_INVALID_PICTURE;
    /* SVT-AV1 validates n_filled_len on every send_picture even when
     * the payload is the planar EbSvtIOFormat (it represents the
     * logical YUV420 byte count, not a raw memcpy size). Without this
     * line, the first send_picture returns EB_ErrorBadParameter
     * (-2147479547) with the misleading message "Invalid API input
     * buffer size detected." */
    const uint32_t bytes_per_sample = (spec->bit_depth == 10) ? 2u : 1u;
    enc->input_hdr.n_filled_len =
        (uint32_t)spec->width * (uint32_t)spec->height * 3u / 2u * bytes_per_sample;

    *out = enc;
    return VMAV_OK_STATUS;
}

void vmav_svtav1_encoder_close(vmav_svtav1_encoder_t *enc) {
    if (enc == NULL) {
        return;
    }
    if (enc->outstanding_pkt != NULL) {
        svt_av1_enc_release_out_buffer(&enc->outstanding_pkt);
    }
    if (enc->svt_handle != NULL) {
        (void)svt_av1_enc_deinit(enc->svt_handle);
        (void)svt_av1_enc_deinit_handle(enc->svt_handle);
    }
    free(enc);
}

vmav_status_t vmav_svtav1_encoder_send(vmav_svtav1_encoder_t *enc,
                                       const uint8_t *const planes[3],
                                       const int linesize[3],
                                       int64_t pts,
                                       bool eos) {
    if (enc == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "send: null encoder");
    }
    if (eos) {
        EbBufferHeaderType eos_hdr = {0};
        eos_hdr.size = sizeof(eos_hdr);
        eos_hdr.flags = EB_BUFFERFLAG_EOS;
        const EbErrorType rc = svt_av1_enc_send_picture(enc->svt_handle, &eos_hdr);
        if (rc != EB_ErrorNone) {
            return VMAV_ERR(VMAV_ERR_FFMPEG, "send_picture(eos): %d", rc);
        }
        return VMAV_OK_STATUS;
    }
    if (planes == NULL || linesize == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "send: null planes/linesize");
    }
    enc->input_pic.luma = (uint8_t *)planes[0];
    enc->input_pic.cb = (uint8_t *)planes[1];
    enc->input_pic.cr = (uint8_t *)planes[2];
    /* SVT-AV1's EbSvtIOFormat.{y,cb,cr}_stride is expressed in SAMPLES,
     * not bytes — but libavformat's AVFrame.linesize[] is in bytes.
     * For 8-bit that's equal; for 10-bit a sample occupies 2 bytes,
     * so we have to divide by bytes_per_sample to convert. Without
     * this, SVT's NEON unpacker (`svt_unpack_and_2bcompress_neon`)
     * walks 2× past each row and SIGSEGVs at the first send_picture
     * with a 10-bit input (HDR HEVC, HEVC Main 10, etc.). */
    const uint32_t bytes_per_sample = (enc->bit_depth == 10) ? 2u : 1u;
    enc->input_pic.y_stride = (uint32_t)linesize[0] / bytes_per_sample;
    enc->input_pic.cb_stride = (uint32_t)linesize[1] / bytes_per_sample;
    enc->input_pic.cr_stride = (uint32_t)linesize[2] / bytes_per_sample;
    enc->input_hdr.pts = pts;
    enc->input_hdr.flags = 0;
    const EbErrorType rc = svt_av1_enc_send_picture(enc->svt_handle, &enc->input_hdr);
    if (rc != EB_ErrorNone) {
        return VMAV_ERR(VMAV_ERR_FFMPEG, "send_picture: %d", rc);
    }
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_svtav1_encoder_recv(vmav_svtav1_encoder_t *enc,
                                       const uint8_t **out_data,
                                       size_t *out_size,
                                       int64_t *out_pts,
                                       bool *out_is_keyframe) {
    if (enc == NULL || out_data == NULL || out_size == NULL || out_pts == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "recv: null arg");
    }
    /* Caller must release previous packet before the next recv. */
    if (enc->outstanding_pkt != NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "recv: outstanding packet — call release first");
    }
    EbBufferHeaderType *pkt = NULL;
    const EbErrorType rc = svt_av1_enc_get_packet(enc->svt_handle, &pkt, /*pic_send_done=*/0);
    if (rc == EB_NoErrorEmptyQueue) {
        return VMAV_ERR(VMAV_ERR_AGAIN, "recv: queue empty");
    }
    if (rc != EB_ErrorNone || pkt == NULL) {
        return VMAV_ERR(VMAV_ERR_FFMPEG, "recv: get_packet rc=%d", rc);
    }
    if ((pkt->flags & EB_BUFFERFLAG_EOS) != 0 && pkt->n_filled_len == 0) {
        svt_av1_enc_release_out_buffer(&pkt);
        return VMAV_ERR(VMAV_ERR_EOF, "recv: end of stream");
    }
    enc->outstanding_pkt = pkt;
    *out_data = pkt->p_buffer;
    *out_size = pkt->n_filled_len;
    *out_pts = pkt->pts;
    if (out_is_keyframe != NULL) {
        *out_is_keyframe = (pkt->pic_type == EB_AV1_KEY_PICTURE);
    }
    return VMAV_OK_STATUS;
}

void vmav_svtav1_encoder_release(vmav_svtav1_encoder_t *enc) {
    if (enc == NULL || enc->outstanding_pkt == NULL) {
        return;
    }
    svt_av1_enc_release_out_buffer(&enc->outstanding_pkt);
    enc->outstanding_pkt = NULL;
}
