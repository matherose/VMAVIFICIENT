#pragma once

#include "vmavificient/vmav_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* libvmaf wrapper. CRF search submits matched (reference, distorted)
 * frame pairs and asks for a pooled VMAF score at the end. Mirrors
 * vmav_svtav1's spec-driven shape. */

typedef struct {
    int width;         /* frame width */
    int height;        /* frame height */
    int bit_depth;     /* 8 or 10; both flow through as YUV420P */
    int n_threads;     /* 0 = auto (n_cpus / 2) */
    const char *model; /* NULL → "vmaf_v0.6.1" (built into libvmaf) */
} vmav_vmaf_spec_t;

typedef struct vmav_vmaf vmav_vmaf_t;

/* Open a new VMAF context for the given spec. The model JSON is
 * embedded in libvmaf.a (built_in_models=true at meson configure),
 * so no model file path is needed. */
vmav_status_t vmav_vmaf_open(const vmav_vmaf_spec_t *spec, vmav_vmaf_t **out);

/* Close and release the context. Safe with NULL. */
void vmav_vmaf_close(vmav_vmaf_t *m);

/* Submit one reference / distorted frame pair. Planes must be
 * YUV420P at the bit depth declared in vmav_vmaf_spec_t. `index`
 * is the frame number — must increase by 1 per call across the
 * lifetime of the context. */
vmav_status_t vmav_vmaf_submit(vmav_vmaf_t *m,
                               const uint8_t *const ref_planes[3],
                               const int ref_strides[3],
                               const uint8_t *const dist_planes[3],
                               const int dist_strides[3],
                               unsigned index);

/* Signal end-of-stream + retrieve the harmonic-mean pooled score
 * across all submitted frames. After this call, no more submits
 * are allowed (caller should close the context). */
vmav_status_t vmav_vmaf_finalize(vmav_vmaf_t *m, double *out_score);

#ifdef __cplusplus
}
#endif
