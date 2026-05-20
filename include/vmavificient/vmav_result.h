#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Project-wide status code enum. The numeric values are stable —
 * downstream code may switch on them — but the set is open: append
 * new codes at the bottom, never reorder. */
typedef enum {
    VMAV_OK = 0,
    VMAV_ERR_GENERIC,
    VMAV_ERR_IO,
    VMAV_ERR_PARSE,
    VMAV_ERR_NO_MEM,
    VMAV_ERR_BAD_ARG,
    VMAV_ERR_NOT_FOUND,
    VMAV_ERR_NOT_IMPL,
    VMAV_ERR_PERMISSION,
    VMAV_ERR_TIMEOUT,
    VMAV_ERR_CANCELED,
    VMAV_ERR_SUBPROC,
    VMAV_ERR_FFMPEG,
    VMAV_ERR_ENCODE,
    VMAV_ERR_DECODE,
    VMAV_ERR_INVARIANT
} vmav_code_t;

/* A status is either VMAV_OK with an empty message, or a non-OK code
 * with a printf-formatted message + the source location where the
 * error was constructed (captured via VMAV_ERR). */
typedef struct {
    vmav_code_t code;
    char msg[256];
    const char *file;
    int line;
} vmav_status_t;

/* Convenience: a zero-initialized vmav_status_t is the success status
 * (VMAV_OK == 0). Use as `return VMAV_OK_STATUS;` or to initialize. */
#define VMAV_OK_STATUS ((vmav_status_t){0})

/* Human-readable name for a status code. Returns a stable C-string;
 * never NULL. Unknown codes return "unknown". */
const char *vmav_code_str(vmav_code_t code);

/* Construct an error status with a printf-style message. Prefer the
 * VMAV_ERR macro, which captures __FILE__ and __LINE__ automatically. */
vmav_status_t vmav_status_make(vmav_code_t code,
                               const char *file,
                               int line,
                               const char *fmt,
                               ...)
    __attribute__((format(printf, 4, 5)));

/* Construct an error status with a captured source location. The
 * `code` argument must be non-OK; passing VMAV_OK is a programming
 * error (the message is still recorded so callers can debug). */
#define VMAV_ERR(code, ...) vmav_status_make((code), __FILE__, __LINE__, __VA_ARGS__)

/* If `expr` evaluates to a non-OK status, return it from the current
 * function. The current function must return vmav_status_t. */
#define VMAV_TRY(expr)                                                                              \
    do {                                                                                            \
        vmav_status_t _vmav_try_st = (expr);                                                        \
        if (_vmav_try_st.code != VMAV_OK) {                                                         \
            return _vmav_try_st;                                                                    \
        }                                                                                           \
    } while (0)

static inline bool vmav_status_ok(vmav_status_t st) {
    return st.code == VMAV_OK;
}

#ifdef __cplusplus
}
#endif
