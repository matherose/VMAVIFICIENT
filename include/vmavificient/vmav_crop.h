#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Crop rectangle suggested by lavfi cropdetect. When `is_meaningful`
 * is false the source has no black bars worth cropping (the rect
 * equals the source dimensions). */
typedef struct {
    int x;
    int y;
    int width;
    int height;
    bool is_meaningful;
} vmav_crop_rect_t;

/* Run lavfi `cropdetect` on the first ~120 frames of the primary
 * video stream and return its best guess. Phase 4's encoder pipeline
 * uses the result to scale-and-crop before SVT-AV1.
 *
 * `is_meaningful` is set to true only when the detected crop trims
 * at least 8 px from one or more sides — encoders shouldn't waste
 * cycles on sub-pixel-precision rounding.
 *
 * Returns the standard probe error codes (IO / NOT_FOUND / FFMPEG).
 */
vmav_status_t vmav_crop_probe(const char *path, vmav_crop_rect_t *out);

#ifdef __cplusplus
}
#endif
