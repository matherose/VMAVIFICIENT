#pragma once

/* Every magic number in the encoder pipeline lives here. Keep this
 * file as the single source of truth: when tuning, edit one place and
 * the change propagates to crf_search, encoder_preset, media_analysis,
 * and any future CLI flags that override these defaults.
 *
 * Values are ported from v1.5.0 (commit 8c1f571 and lineage); changing
 * them changes encoded output, so coordinate updates with the
 * regression-fixtures plan (Phase 5+). */

/* === Resolution thresholds ==================================== */

/* >= this height selects the 4K-tier preset/bitrate. */
#define VMAV_HEIGHT_4K_THRESHOLD 2160

/* === CRF search (libSvtAv1Enc + libvmaf binary search) ======== */

#define VMAV_CRF_MIN_DEFAULT          18
#define VMAV_CRF_MAX_DEFAULT          50
#define VMAV_CRF_INITIAL_GUESS        30
#define VMAV_CRF_DEFAULT_SLOPE        (-0.6)
#define VMAV_CRF_CONVERGENCE_DELTA    0.5
#define VMAV_CRF_MAX_TRIALS           4
#define VMAV_CRF_MIN_SLOPE_THRESHOLD  0.05 /* below = use default slope */
#define VMAV_CRF_PROBE_SAMPLE_FRAMES  480  /* 20s @ 24fps */
#define VMAV_CRF_ESCALATE_DELTA       2.0  /* |target-vmaf| > this => 3 samples */
#define VMAV_CRF_ESCALATED_N_SAMPLES  3

/* === VMAF targets per content type + resolution =============== */

#define VMAV_VMAF_LIVE_ACTION_4K  92
#define VMAV_VMAF_LIVE_ACTION_HD  93
#define VMAV_VMAF_ANIMATION_4K    96
#define VMAV_VMAF_ANIMATION_HD    96
#define VMAV_VMAF_SUPER35_4K      93
#define VMAV_VMAF_SUPER35_HD      94
#define VMAV_VMAF_IMAX_4K         94
#define VMAV_VMAF_IMAX_HD         95

/* === Bitrate tiers (kbps) for the no-CRF VBR fallback ========= */

#define VMAV_BITRATE_4K_GRAINY    4000
#define VMAV_BITRATE_4K_CLEAN     3500
#define VMAV_BITRATE_4K_ANIMATION 3000
#define VMAV_BITRATE_HD_GRAINY    2500
#define VMAV_BITRATE_HD_CLEAN     2000
#define VMAV_BITRATE_HD_ANIMATION 1500

/* === Grain analysis (per-window signalstats sampling) ========= */

#define VMAV_GRAIN_NUM_WINDOWS              4
#define VMAV_GRAIN_LOW_THRESHOLD            0.08 /* below = no extra denoise tuning */
#define VMAV_GRAIN_HIGH_THRESHOLD           0.15 /* above = aggressive film-grain handling */
#define VMAV_GRAIN_VARIANCE_HIGH_THRESHOLD  0.0040
#define VMAV_GRAIN_BRACKET_BOUNDARY_PROXIMITY 0.10

/* === Audio (Opus encoder defaults) ============================ */

#define VMAV_OPUS_KBPS_PER_CHANNEL  64
#define VMAV_OPUS_VBR_LEVEL         10
#define VMAV_OPUS_SAMPLE_RATE_HZ    48000

/* === Process / subprocess timeouts (ms) ======================= */

#define VMAV_PROBE_TIMEOUT_MS       10000
#define VMAV_FFMPEG_DEFAULT_TIMEOUT_MS 600000 /* 10 minutes */
