#pragma once

#include "vmavificient/vmav_naming.h"
#include "vmavificient/vmav_result.h"
#include "vmavificient/vmav_tracks.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PGS bitmap subtitle → SRT text conversion via Tesseract OCR.
 *
 * Pipeline: FFmpeg demux → native PGS segment parsing (PCS/PDS/ODS/WDS/END)
 *           → RLE decode → palette apply (YCbCrA → 8-bit binarized PIX)
 *           → leptonica pad + scale + Otsu binarize → Tesseract OCR
 *           → SRT lines.
 *
 * v1 had this at src/subtitle_convert/subtitle_convert.c. The port
 * keeps the same algorithm bit-for-bit — the SRT golden tests rely
 * on it. */

typedef struct {
    char output_path[1024];
    bool skipped;       /* true if output existed and was reused */
    int subtitle_count; /* number of subtitles emitted */
} vmav_subtitle_convert_t;

/* Pure-function helpers. */

/* Map an ISO 639-2/B 3-letter code to its Tesseract tessdata name.
 * Returns "eng" for NULL/empty input; passes unknown codes through
 * unchanged so a custom traineddata can be addressed by its raw name. */
const char *vmav_subtitle_iso639_to_tesseract(const char *iso639);

/* Build the canonical SRT filename for `track`. Schema (matches v1):
 *   <base>.<lang>.full.srt              non-French, normal track
 *   <base>.<lang>.forced.srt            non-French, forced
 *   <base>.<lang>.sdh.srt               non-French, SDH/CC
 *   <base>.fre.fr..full.srt             French VFF / default
 *   <base>.fre.ca..forced.srt           French VFQ + forced
 *   <base>.fre.vfi..sdh.srt             French VFI + SDH
 * (The double dot between fre.xx and type is v1's quirk — preserved
 * for naming compatibility with the rest of the v1 toolchain.) */
void vmav_subtitle_build_srt_filename(char *buf,
                                      size_t bufsize,
                                      const char *base_name,
                                      const char *language,
                                      vmav_naming_french_variant_t fv,
                                      bool is_forced,
                                      bool is_sdh);

/* True if `track` carries HDMV PGS bitmaps (the format we OCR). */
bool vmav_subtitle_is_pgs(const vmav_track_t *track);

/* True if `track` carries plain text subtitles (no OCR needed —
 * the caller can pass them through to muxing as-is). */
bool vmav_subtitle_is_text(const vmav_track_t *track);

/* Convert one PGS subtitle track to an SRT file. `tesseract_lang`
 * overrides the auto-detected tessdata name (mapped from
 * `track->language`); pass NULL/"" to accept the default. Skips the
 * work if `output_path` already exists with non-zero size. Returns
 * VMAV_ERR_* on failure with partial output removed. */
vmav_status_t vmav_subtitle_convert_pgs(const char *input_path,
                                        const vmav_track_t *track,
                                        const char *output_path,
                                        const char *tesseract_lang,
                                        vmav_subtitle_convert_t *out);

#ifdef __cplusplus
}
#endif
