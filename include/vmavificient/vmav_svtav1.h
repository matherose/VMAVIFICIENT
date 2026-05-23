#pragma once

#include "vmavificient/vmav_preset.h"
#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SVT-AV1-HDR encoder wrapper. Phase 4's encoder driver + CRF search
 * use this module to:
 *   1. Map a high-level vmav_preset_t + resolution onto the 50+ knob
 *      EbSvtAv1EncConfiguration we ship to SVT-AV1.
 *   2. Copy FFmpeg-discovered HDR metadata (mastering display + CLL)
 *      into the encoder config.
 *   3. Run the actual encode loop via init_handle → set_parameter →
 *      svt_av1_enc_init → svt_av1_enc_send_picture / get_packet → deinit.
 *
 * The preset parameter tables are direct ports of v1's
 * src/encode_preset/encode_preset.c values — calibrated for SVT-AV1-HDR
 * v4.1.0+ at preset 4. Bumping the encoder fork may require re-tuning. */

/* Encoder spec — what consumer fills in before opening an encoder. */
typedef struct {
    vmav_preset_t preset;    /* selects the parameter table */
    int width;               /* video frame width */
    int height;              /* video frame height (selects 4K vs HD tier) */
    int bit_depth;           /* 8 or 10; SVT-AV1 supports both */
    int fps_num;             /* AVStream::r_frame_rate.num */
    int fps_den;             /* AVStream::r_frame_rate.den */
    int film_grain;          /* 0..50 — from vmav_svtav1_film_grain_from_score */
    int target_bitrate_kbps; /* used when crf <= 0 */
    int crf;                 /* CRF 1..63, or <= 0 for VBR */
    int color_primaries;     /* AVCodecParameters::color_primaries */
    int color_trc;           /* AVCodecParameters::color_trc */
    int color_space;         /* AVCodecParameters::color_space */
    int color_range;         /* AVCOL_RANGE_* — JPEG=full, MPEG=studio */
    /* Optional HDR10 metadata. The struct fields below mirror
     * AVMasteringDisplayMetadata + AVContentLightMetadata; set
     * has_mastering / has_cll true when they're populated. */
    bool has_mastering;
    double mastering_red_x, mastering_red_y;
    double mastering_green_x, mastering_green_y;
    double mastering_blue_x, mastering_blue_y;
    double mastering_white_x, mastering_white_y;
    double mastering_max_luma; /* nits */
    double mastering_min_luma; /* nits */
    bool has_cll;
    uint16_t cll_max_cll;
    uint16_t cll_max_fall;
} vmav_svtav1_spec_t;

/* Opaque encoder handle. Owns the EbComponentType* + the input/output
 * EbBufferHeaderType allocations. */
typedef struct vmav_svtav1_encoder vmav_svtav1_encoder_t;

/* Open a new encoder for the given spec. Returns VMAV_OK_STATUS on
 * success and writes the new handle to *out. On failure, *out is
 * left NULL and no cleanup is required. */
vmav_status_t vmav_svtav1_encoder_open(const vmav_svtav1_spec_t *spec, vmav_svtav1_encoder_t **out);

/* Close the encoder and release all resources. Safe to call with NULL. */
void vmav_svtav1_encoder_close(vmav_svtav1_encoder_t *enc);

/* Send one input frame to the encoder.
 * `planes[3]` — Y, U, V plane pointers (8-bit) or interleaved 10-bit.
 * `linesize[3]` — bytes per row for each plane.
 * `pts` — presentation timestamp in encoder time-base units.
 * Pass `eos = true` to signal end-of-stream (planes/linesize ignored). */
vmav_status_t vmav_svtav1_encoder_send(vmav_svtav1_encoder_t *enc,
                                       const uint8_t *const planes[3],
                                       const int linesize[3],
                                       int64_t pts,
                                       bool eos);

/* Retrieve one encoded packet from the encoder.
 *
 * On success, writes the encoded AV1 bytes to (*out_data, *out_size)
 * and the PTS to *out_pts. The buffer is owned by the encoder — caller
 * must call vmav_svtav1_encoder_release after consuming.
 *
 * Returns:
 *   VMAV_OK_STATUS              — packet ready (call release after use)
 *   VMAV_ERR(VMAV_ERR_AGAIN)    — no packet ready yet, send more frames
 *   VMAV_ERR(VMAV_ERR_EOF)      — end of stream
 *   other VMAV_ERR_*            — encoder error */
vmav_status_t vmav_svtav1_encoder_recv(vmav_svtav1_encoder_t *enc,
                                       const uint8_t **out_data,
                                       size_t *out_size,
                                       int64_t *out_pts,
                                       bool *out_is_keyframe);

/* Release the packet returned by the last successful recv. Required
 * before the next recv call. */
void vmav_svtav1_encoder_release(vmav_svtav1_encoder_t *enc);

/* Pure helpers (used by crf_search + smoke tests; no encoder handle
 * needed). */

/* Map a grain score (0..1) and per-window variance to an SVT-AV1
 * `--noise` or `--film-grain` strength (0..50). Per-preset curves:
 * analog film maps aggressively, animation clamps to near-zero,
 * digital sits in between. */
int vmav_svtav1_film_grain_from_score(double grain_score,
                                      double grain_variance,
                                      vmav_preset_t preset);

#ifdef __cplusplus
}
#endif
