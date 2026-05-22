#pragma once

#include "vmavificient/vmav_naming.h"
#include "vmavificient/vmav_result.h"
#include "vmavificient/vmav_tracks.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio track encoding to .opus.
 *
 * One track in, one .opus file out. Encoding pipeline:
 *   input → libavcodec decode → libswresample to 48kHz float
 *         → AVAudioFifo → libavcodec libopus encoder → ogg muxer
 *
 * Bitrate: 56 kbps per channel (standard scene-quality target — gives
 * roughly 112 kbps stereo, 280 kbps 5.1, 336 kbps 7.1). Multichannel
 * uses opus mapping_family=1 (Vorbis channel order) for correct 5.1+
 * layouts.
 *
 * libopus only accepts standard channel layouts (mono, stereo, 5.1,
 * 7.1), so the encoder is configured with `av_channel_layout_default`
 * to match the input's channel COUNT rather than the input's exact
 * layout (which may be e.g. 5.1(side) or 5.1(back)). The swresampler
 * then handles the layout remapping. */

typedef struct {
    char output_path[1024];
    bool skipped; /* true if output existed and was reused */
} vmav_audio_encode_t;

/* Build the canonical .opus filename for `track`:
 *   <base>.<lang>.opus            — non-French
 *   <base>.fre.fr.opus             — French (default / VFF)
 *   <base>.fre.ca.opus             — French (VFQ)
 *   <base>.fre.vfi.opus            — French (VFI)
 * `language` is the ISO 639-2/B 3-letter code from the track metadata. */
void vmav_audio_build_filename(char *buf,
                               size_t bufsize,
                               const char *base_name,
                               const char *language,
                               vmav_naming_french_variant_t fv);

/* Encode `track` (one audio stream of `input_path`) to `output_path`.
 * Skips the work if `output_path` already exists with non-zero size.
 * Returns VMAV_ERR_* status; on failure any partial output is deleted. */
vmav_status_t vmav_audio_encode_track(const char *input_path,
                                      const vmav_track_t *track,
                                      const char *output_path,
                                      vmav_audio_encode_t *out);

#ifdef __cplusplus
}
#endif
