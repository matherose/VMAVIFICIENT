/* Smoke test for the vmav_ssimu2 FFI.
 *
 * Builds a pair of synthetic YUV420P10 frames and scores them. Expects:
 *   - identical frames  -> score ~= 100
 *   - perturbed  frame  -> score noticeably lower
 *   - null pointers     -> VMAV_SSIMU2_ERROR (-1.0)
 *
 * Build (from project root):
 *   clang -std=c11 -O2 -Iinclude rust/vmav_ssimu2/test_smoke.c \
 *     build/deps/vmav_ssimu2/lib/libvmav_ssimu2.a \
 *     -framework CoreFoundation -framework Security -lSystem \
 *     -o build/test_smoke
 */

#include "media_ssimu2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Libav AVColor* numeric values (ITU-T H.273).
 * These match the values in libavutil/pixfmt.h exactly, which is the whole
 * point of the FFI design — the C side never needs to remap. */
#define AVCOL_PRI_BT2020      9
#define AVCOL_TRC_SMPTE2084  16
#define AVCOL_SPC_BT2020_NCL  9

#define AVCOL_PRI_BT709       1
#define AVCOL_TRC_BT709       1
#define AVCOL_SPC_BT709       1

static void fill_u16_plane(uint8_t *buf, size_t stride_bytes, uint32_t height,
                           uint16_t value) {
    /* little-endian u16, same value every pixel */
    for (uint32_t row = 0; row < height; ++row) {
        uint16_t *row_ptr = (uint16_t *)(buf + row * stride_bytes);
        size_t pix_per_row = stride_bytes / 2;
        for (size_t x = 0; x < pix_per_row; ++x) {
            row_ptr[x] = value;
        }
    }
}

int main(void) {
    /* Small enough to keep the test fast; big enough to exercise chroma
     * subsampling. 256x256 = 64k luma pixels. */
    const uint32_t W = 256, H = 256;
    const uint32_t y_stride  = W * 2;
    const uint32_t uv_stride = (W / 2) * 2;
    const size_t y_bytes  = (size_t)y_stride  * H;
    const size_t uv_bytes = (size_t)uv_stride * (H / 2);

    /* Allocate two sets of planes. */
    uint8_t *ref_y = calloc(1, y_bytes),  *dis_y = calloc(1, y_bytes);
    uint8_t *ref_u = calloc(1, uv_bytes), *dis_u = calloc(1, uv_bytes);
    uint8_t *ref_v = calloc(1, uv_bytes), *dis_v = calloc(1, uv_bytes);
    assert(ref_y && dis_y && ref_u && dis_u && ref_v && dis_v);

    /* 10-bit mid-gray (limited range). Y=512, U=V=512. */
    fill_u16_plane(ref_y, y_stride,  H,     512);
    fill_u16_plane(ref_u, uv_stride, H / 2, 512);
    fill_u16_plane(ref_v, uv_stride, H / 2, 512);
    memcpy(dis_y, ref_y, y_bytes);
    memcpy(dis_u, ref_u, uv_bytes);
    memcpy(dis_v, ref_v, uv_bytes);

    int failures = 0;

    /* ---- Test 1: identical SDR BT.709 frames -> ~100 ---- */
    double s1 = vmav_ssimu2_score_yuv420p10(
        ref_y, ref_u, ref_v, dis_y, dis_u, dis_v, W, H, y_stride, uv_stride,
        AVCOL_PRI_BT709, AVCOL_TRC_BT709, AVCOL_SPC_BT709, false);
    printf("[1] identical SDR BT.709        : %.4f\n", s1);
    if (s1 < 99.0 || s1 > 100.1) {
        fprintf(stderr, "    FAIL: expected ~100, got %.4f\n", s1);
        failures++;
    }

    /* ---- Test 2: identical HDR BT.2020/PQ -> ~100 ----
     * This exercises the HDR-native path: no tonemap, no libzimg. */
    double s2 = vmav_ssimu2_score_yuv420p10(
        ref_y, ref_u, ref_v, dis_y, dis_u, dis_v, W, H, y_stride, uv_stride,
        AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, AVCOL_SPC_BT2020_NCL, false);
    printf("[2] identical HDR BT.2020/PQ    : %.4f\n", s2);
    if (s2 < 99.0 || s2 > 100.1) {
        fprintf(stderr, "    FAIL: expected ~100, got %.4f\n", s2);
        failures++;
    }

    /* ---- Test 3: perturbed distorted luma -> noticeably lower ----
     * Shift the distorted luma plane +40 (about 4% of 10-bit range). */
    fill_u16_plane(dis_y, y_stride, H, 552);
    double s3 = vmav_ssimu2_score_yuv420p10(
        ref_y, ref_u, ref_v, dis_y, dis_u, dis_v, W, H, y_stride, uv_stride,
        AVCOL_PRI_BT709, AVCOL_TRC_BT709, AVCOL_SPC_BT709, false);
    printf("[3] perturbed luma (+40)        : %.4f\n", s3);
    if (s3 >= s1) {
        fprintf(stderr, "    FAIL: perturbed should score lower than identical\n");
        failures++;
    }

    /* ---- Test 4: null pointer -> error sentinel ---- */
    double s4 = vmav_ssimu2_score_yuv420p10(
        NULL, ref_u, ref_v, dis_y, dis_u, dis_v, W, H, y_stride, uv_stride,
        AVCOL_PRI_BT709, AVCOL_TRC_BT709, AVCOL_SPC_BT709, false);
    printf("[4] null ref_y                  : %.4f (expect %.1f)\n",
           s4, (double)VMAV_SSIMU2_ERROR);
    if (s4 != VMAV_SSIMU2_ERROR) {
        fprintf(stderr, "    FAIL: expected VMAV_SSIMU2_ERROR\n");
        failures++;
    }

    /* ---- Test 5: bogus color primaries -> error sentinel ---- */
    double s5 = vmav_ssimu2_score_yuv420p10(
        ref_y, ref_u, ref_v, dis_y, dis_u, dis_v, W, H, y_stride, uv_stride,
        9999, AVCOL_TRC_BT709, AVCOL_SPC_BT709, false);
    printf("[5] invalid color primaries     : %.4f (expect %.1f)\n",
           s5, (double)VMAV_SSIMU2_ERROR);
    if (s5 != VMAV_SSIMU2_ERROR) {
        fprintf(stderr, "    FAIL: expected VMAV_SSIMU2_ERROR\n");
        failures++;
    }

    free(ref_y); free(dis_y);
    free(ref_u); free(dis_u);
    free(ref_v); free(dis_v);

    if (failures == 0) {
        printf("\nALL PASS\n");
        return 0;
    }
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
}
