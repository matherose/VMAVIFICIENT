/* tests/fixtures/fixturegen.c
 *
 * Build-time helper that writes a small synthetic Y4M file. Used by the
 * integration test suite as a stand-in for real media — Y4M is the
 * lowest-common-denominator container libavformat reads natively, and
 * generating one needs only stdio, so this binary cross-compiles
 * cleanly (incl. to Windows via llvm-mingw + wine).
 *
 * Output format: YUV4MPEG2, 320x180, 4:2:0 (C420jpeg), 24 fps, 24 frames
 * (== exactly 1 second). Y plane is a horizontal gradient that shifts
 * across frames so successive frames are not byte-identical; chroma is
 * constant gray. Total fixture size: ~2 MiB.
 *
 * The output path is taken from argv[1]; the parent directory must
 * already exist (CMake creates it via add_custom_command OUTPUT). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dimensions must be 16-aligned — SVT-AV1 internally rounds the input
 * to a multiple of 8 and then expects the API buffer to match the
 * rounded size, so a mismatched 320x180 fails with "Invalid API input
 * buffer size detected" at the first send_picture. 320x192 keeps the
 * fixture small (~2 MiB) while satisfying both 8- and 16-alignment. */
enum {
    FX_WIDTH = 320,
    FX_HEIGHT = 192,
    FX_FPS_NUM = 24,
    FX_FPS_DEN = 1,
    FX_FRAMES = 24,
};

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.y4m>\n", argv[0] ? argv[0] : "fixturegen");
        return 2;
    }

    FILE *f = fopen(argv[1], "wb");
    if (f == NULL) {
        fprintf(stderr, "fixturegen: open '%s' failed\n", argv[1]);
        return 1;
    }

    /* Y4M signature line: explicit progressive frames (Ip), 1:1 SAR,
     * 4:2:0 with JPEG-range chroma siting (matches what most encoders
     * default to for synthetic content). */
    if (fprintf(f,
                "YUV4MPEG2 W%d H%d F%d:%d Ip A1:1 C420jpeg\n",
                FX_WIDTH,
                FX_HEIGHT,
                FX_FPS_NUM,
                FX_FPS_DEN) < 0) {
        fclose(f);
        return 1;
    }

    const size_t y_size = (size_t)FX_WIDTH * (size_t)FX_HEIGHT;
    const size_t uv_size = (size_t)(FX_WIDTH / 2) * (size_t)(FX_HEIGHT / 2);
    const size_t frame_bytes = y_size + (2 * uv_size);
    uint8_t *frame = malloc(frame_bytes);
    if (frame == NULL) {
        fclose(f);
        return 1;
    }
    /* Chroma is constant gray for the whole file — write once, never
     * touch again. The luma plane is overwritten per frame below. */
    memset(frame + y_size, 128, 2 * uv_size);

    for (int t = 0; t < FX_FRAMES; t++) {
        for (int y = 0; y < FX_HEIGHT; y++) {
            for (int x = 0; x < FX_WIDTH; x++) {
                /* Horizontal gradient shifted by frame index so every
                 * frame is distinct and lossless encoders can't trivially
                 * compress to a single intra. */
                frame[((size_t)y * FX_WIDTH) + (size_t)x] = (uint8_t)((x + (t * 8)) & 0xFF);
            }
        }
        if (fputs("FRAME\n", f) == EOF || fwrite(frame, 1, frame_bytes, f) != frame_bytes) {
            free(frame);
            fclose(f);
            return 1;
        }
    }

    free(frame);
    if (fclose(f) != 0) {
        return 1;
    }
    return 0;
}
