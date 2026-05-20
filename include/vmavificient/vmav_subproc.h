#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A growable byte buffer. Owned by the result struct that contains it;
 * free via vmav_subproc_result_free. `data` is either NULL (no bytes
 * captured) or a malloc'd NUL-terminated string of length `len`. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} vmav_buf_t;

/* Description of a child process to spawn. */
typedef struct {
    /* Executable name or path. If it contains no '/', PATH is searched.
     * Otherwise the path is used directly. Must be non-NULL. */
    const char *exe;

    /* NULL-terminated argv array. argv[0] should match the basename of
     * `exe`; the spawned process sees argv[0] as its name. */
    const char *const *argv;

    /* Optional NULL-terminated envp array. NULL inherits the parent's
     * environment. */
    const char *const *envp;

    /* Optional working directory. Currently NOT supported (posix_spawn
     * `addchdir_np` portability varies); set to NULL or expect
     * VMAV_ERR_NOT_IMPL. */
    const char *cwd;

    /* When true, the child's stdout / stderr is captured into the
     * result. When false, the stream is left attached to the parent's
     * (i.e. it goes to the terminal). */
    bool capture_stdout;
    bool capture_stderr;

    /* Max wall-clock ms before the child is terminated. 0 = no
     * timeout. On timeout, the child is sent SIGTERM, given 200 ms
     * grace, then SIGKILL. result.timed_out is set true. */
    uint32_t timeout_ms;
} vmav_subproc_spec_t;

/* Outcome of a child process. Initialized by vmav_subproc_run; must be
 * freed by vmav_subproc_result_free even on failure. */
typedef struct {
    /* Exit status: 0..255 if exited normally; 128+signum if signaled. */
    int exit_code;

    /* True if the child was killed because timeout_ms was reached. */
    bool timed_out;

    /* True if exited via signal; signal_num is the signal number. */
    bool signaled;
    int signal_num;

    /* Captured streams. data is NULL when nothing was captured (either
     * not requested, or the stream was empty). When non-NULL, data is
     * NUL-terminated; len excludes the NUL. */
    vmav_buf_t stdout_buf;
    vmav_buf_t stderr_buf;

    /* Wall-clock duration in milliseconds. */
    uint64_t wall_ms;
} vmav_subproc_result_t;

/* Spawn a child, capture optionally, wait for exit (or timeout). On
 * success returns VMAV_OK_STATUS; on failure returns an error status
 * and `out` is reset. Even on success, `out` may contain non-zero
 * exit_code / signaled / timed_out — those are normal outcomes the
 * caller must inspect. */
vmav_status_t vmav_subproc_run(const vmav_subproc_spec_t *spec, vmav_subproc_result_t *out);

/* Release any heap-allocated buffers in `out`. Safe to call on a
 * zeroed struct. Idempotent. */
void vmav_subproc_result_free(vmav_subproc_result_t *out);

#ifdef __cplusplus
}
#endif
