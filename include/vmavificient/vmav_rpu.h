#pragma once

#include "vmavificient/vmav_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dolby Vision RPU extraction.
 *
 * Reads an HEVC input video, picks out HEVC UNSPEC62 NAL units (DV
 * carries its RPU there), parses them via libdovi, and writes the
 * normalized RPU payload to disk as a length-prefixed sidecar
 * (4-byte big-endian length + payload, repeating per RPU). The output
 * format matches what `dovi_tool extract-rpu` produces and what
 * SVT-AV1-HDR consumes via `--dolby-vision-rpu`. */

typedef struct {
    char output_path[1024]; /* path written (or attempted) */
    int rpu_count;          /* number of RPUs successfully parsed */
    bool skipped;           /* true if output already existed */
} vmav_rpu_extract_t;

/* Build the canonical sidecar filename: `<base>.rpu.bin`. */
void vmav_rpu_build_filename(char *buf, size_t bufsize, const char *base_name);

/* Extract DV RPUs from `input_path` and write to `output_path`. If
 * `output_path` exists and is non-empty, no work is done and
 * `out->skipped` is set. On failure the partial output (if any) is
 * deleted. Returns VMAV_ERR_NOT_FOUND if the input has no DV NAL units. */
vmav_status_t
vmav_rpu_extract(const char *input_path, const char *output_path, vmav_rpu_extract_t *out);

#ifdef __cplusplus
}
#endif
