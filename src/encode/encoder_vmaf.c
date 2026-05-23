/* libvmaf wrapper.
 *
 * Phase 4's CRF search submits matched (reference, distorted) frame
 * pairs and asks for a final harmonic-mean pooled VMAF score. The
 * wrapper encapsulates libvmaf's three-step init (vmaf_init,
 * vmaf_model_load, vmaf_use_features_from_model) and the
 * picture-alloc + copy + read_pictures + score_pooled lifecycle. */

#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_vmaf.h"

#include <stdlib.h>
#include <string.h>

#include <libvmaf/libvmaf.h>

#define VMAF_DEFAULT_MODEL "vmaf_v0.6.1"

struct vmav_vmaf {
    VmafContext *ctx;
    VmafModel *model;
    int width;
    int height;
    int bit_depth;
    unsigned frames_submitted; /* runs from 0 — sanity-checks index */
    unsigned next_expected_index;
    bool finalized;
};

vmav_status_t vmav_vmaf_open(const vmav_vmaf_spec_t *spec, vmav_vmaf_t **out) {
    if (spec == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_vmaf_open: null arg");
    }
    *out = NULL;
    if (spec->width <= 0 || spec->height <= 0) {
        return VMAV_ERR(
            VMAV_ERR_BAD_ARG, "vmav_vmaf_open: bad dims %dx%d", spec->width, spec->height);
    }
    if (spec->bit_depth != 8 && spec->bit_depth != 10) {
        return VMAV_ERR(
            VMAV_ERR_BAD_ARG, "vmav_vmaf_open: bit_depth %d (need 8 or 10)", spec->bit_depth);
    }

    vmav_vmaf_t *m = calloc(1, sizeof(*m));
    if (m == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "calloc vmaf");
    }
    m->width = spec->width;
    m->height = spec->height;
    m->bit_depth = spec->bit_depth;

    VmafConfiguration cfg = {
        .log_level = VMAF_LOG_LEVEL_ERROR,
        .n_threads = (spec->n_threads > 0) ? (unsigned)spec->n_threads : 0,
        .n_subsample = 1,
        .cpumask = 0,
    };
    int rc = vmaf_init(&m->ctx, cfg);
    if (rc < 0) {
        free(m);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "vmaf_init: %d", rc);
    }

    const char *model_name = (spec->model != NULL) ? spec->model : VMAF_DEFAULT_MODEL;
    VmafModelConfig mcfg = {.name = "vmaf"};
    rc = vmaf_model_load(&m->model, &mcfg, model_name);
    if (rc < 0) {
        vmaf_close(m->ctx);
        free(m);
        return VMAV_ERR(VMAV_ERR_FFMPEG,
                        "vmaf_model_load(%s): %d (built-in models: vmaf_v0.6.1, "
                        "vmaf_v0.6.1neg, vmaf_4k_v0.6.1)",
                        model_name,
                        rc);
    }
    rc = vmaf_use_features_from_model(m->ctx, m->model);
    if (rc < 0) {
        vmaf_model_destroy(m->model);
        vmaf_close(m->ctx);
        free(m);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "vmaf_use_features_from_model: %d", rc);
    }

    *out = m;
    return VMAV_OK_STATUS;
}

void vmav_vmaf_close(vmav_vmaf_t *m) {
    if (m == NULL) {
        return;
    }
    if (m->model != NULL) {
        vmaf_model_destroy(m->model);
    }
    if (m->ctx != NULL) {
        vmaf_close(m->ctx);
    }
    free(m);
}

/* Copy one YUV420P plane stack into a freshly-allocated VmafPicture. */
static vmav_status_t alloc_and_copy_pic(VmafPicture *pic,
                                        const uint8_t *const planes[3],
                                        const int strides[3],
                                        int width,
                                        int height,
                                        int bit_depth) {
    const enum VmafPixelFormat fmt = VMAF_PIX_FMT_YUV420P;
    if (vmaf_picture_alloc(pic, fmt, (unsigned)bit_depth, (unsigned)width, (unsigned)height) < 0) {
        return VMAV_ERR(
            VMAV_ERR_NO_MEM, "vmaf_picture_alloc %dx%d %dbit", width, height, bit_depth);
    }
    /* YUV420P plane geometry: Y full, U/V quarter-size. Bytes per pixel
     * is 1 for 8-bit, 2 for 10-bit (packed in 16). */
    const int bpp = (bit_depth > 8) ? 2 : 1;
    const int planes_w[3] = {width, width / 2, width / 2};
    const int planes_h[3] = {height, height / 2, height / 2};
    for (int p = 0; p < 3; p++) {
        const uint8_t *src = planes[p];
        uint8_t *dst = pic->data[p];
        const size_t row_bytes = (size_t)planes_w[p] * (size_t)bpp;
        for (int y = 0; y < planes_h[p]; y++) {
            memcpy(dst + (size_t)y * (size_t)pic->stride[p],
                   src + (size_t)y * (size_t)strides[p],
                   row_bytes);
        }
    }
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_vmaf_submit(vmav_vmaf_t *m,
                               const uint8_t *const ref_planes[3],
                               const int ref_strides[3],
                               const uint8_t *const dist_planes[3],
                               const int dist_strides[3],
                               unsigned index) {
    if (m == NULL || ref_planes == NULL || ref_strides == NULL || dist_planes == NULL ||
        dist_strides == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_vmaf_submit: null arg");
    }
    if (m->finalized) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_vmaf_submit: called after finalize");
    }
    if (index != m->next_expected_index) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG,
                        "vmav_vmaf_submit: index gap (got %u, expected %u)",
                        index,
                        m->next_expected_index);
    }

    VmafPicture pic_ref = {0};
    VmafPicture pic_dist = {0};
    vmav_status_t st =
        alloc_and_copy_pic(&pic_ref, ref_planes, ref_strides, m->width, m->height, m->bit_depth);
    if (!vmav_status_ok(st)) {
        return st;
    }
    st =
        alloc_and_copy_pic(&pic_dist, dist_planes, dist_strides, m->width, m->height, m->bit_depth);
    if (!vmav_status_ok(st)) {
        vmaf_picture_unref(&pic_ref);
        return st;
    }

    const int rc = vmaf_read_pictures(m->ctx, &pic_ref, &pic_dist, index);
    /* read_pictures takes ownership of the picture data on success;
     * on failure we still need to unref them. */
    if (rc < 0) {
        vmaf_picture_unref(&pic_ref);
        vmaf_picture_unref(&pic_dist);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "vmaf_read_pictures(index=%u): %d", index, rc);
    }
    m->frames_submitted++;
    m->next_expected_index = index + 1;
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_vmaf_finalize(vmav_vmaf_t *m, double *out_score) {
    if (m == NULL || out_score == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_vmaf_finalize: null arg");
    }
    if (m->finalized) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_vmaf_finalize: already finalized");
    }
    if (m->frames_submitted == 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_vmaf_finalize: no frames submitted");
    }
    /* Flush — passes (NULL, NULL) to signal EOS. */
    const int flush_rc = vmaf_read_pictures(m->ctx, NULL, NULL, 0);
    if (flush_rc < 0) {
        return VMAV_ERR(VMAV_ERR_FFMPEG, "vmaf_read_pictures(flush): %d", flush_rc);
    }
    double score = 0.0;
    const int pool_rc = vmaf_score_pooled(
        m->ctx, m->model, VMAF_POOL_METHOD_HARMONIC_MEAN, &score, 0, m->frames_submitted - 1);
    if (pool_rc < 0) {
        return VMAV_ERR(
            VMAV_ERR_FFMPEG, "vmaf_score_pooled (n=%u): %d", m->frames_submitted, pool_rc);
    }
    *out_score = score;
    m->finalized = true;
    return VMAV_OK_STATUS;
}
