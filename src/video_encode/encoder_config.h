/**
 * @file encoder_config.h
 * @brief Shared SVT-AV1-HDR encoder configuration helpers.
 *
 * Used by both the final encode (video_encode.c) and the CRF probe encodes
 * (crf_search.c) so probe params can't drift from production params.
 */

#ifndef VMAV_ENCODER_CONFIG_H
#define VMAV_ENCODER_CONFIG_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <EbSvtAv1Enc.h>

#include "encode_preset.h"

/**
 * Translate an EncodePreset into an EbSvtAv1EncConfiguration.
 *
 * When @p crf > 0, configures CRF/CQP mode; otherwise configures VBR using
 * @p target_bitrate_kbps. Probe encodes always pass crf > 0.
 *
 * @param cfg                  Pre-zeroed config to populate.
 * @param p                    Active preset.
 * @param film_grain           Grain level (0–50).
 * @param target_bitrate_kbps  Used only in VBR mode (crf <= 0).
 * @param crf                  CRF/CQP value, or 0/-1 for VBR.
 */
void apply_preset_to_config(EbSvtAv1EncConfiguration *cfg,
                            const EncodePreset *p, int film_grain,
                            int target_bitrate_kbps, int crf);

/**
 * Copy primaries / transfer / matrix / range from the source codecpar.
 * Must be called before svt_av1_enc_set_parameter.
 */
void copy_color_info(EbSvtAv1EncConfiguration *cfg,
                     const AVCodecParameters *codecpar);

/**
 * Extract HDR10 static metadata (MDCV + CLL) from stream side data and
 * write it into @p cfg. No-op when the source carries neither.
 * Must be called before svt_av1_enc_set_parameter.
 */
void set_hdr10_metadata(EbSvtAv1EncConfiguration *cfg,
                        const AVStream *stream);

#endif /* VMAV_ENCODER_CONFIG_H */
