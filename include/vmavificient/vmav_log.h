#pragma once

#include "vmavificient/vmav_result.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VMAV_LL_TRACE = 0,
    VMAV_LL_DEBUG,
    VMAV_LL_INFO,
    VMAV_LL_WARN,
    VMAV_LL_ERROR
} vmav_log_level_t;

typedef enum {
    VMAV_LOG_SINK_STDERR = 0, /* human-readable, color when TTY */
    VMAV_LOG_SINK_FILE,       /* plain text appended to a file */
    VMAV_LOG_SINK_JSON_LINES, /* one JSON object per line on stderr */
    VMAV_LOG_SINK_NONE        /* drop all logs (useful in tests) */
} vmav_log_sink_t;

/* Configure the logger. Safe to call before main() finishes
 * initialization; on first call sets reasonable defaults if not yet
 * touched (level=INFO, sink=STDERR). Subsequent calls override. */
void vmav_log_init(vmav_log_level_t level, vmav_log_sink_t sink);

/* Change the minimum visible level at runtime. */
void vmav_log_set_level(vmav_log_level_t level);

/* Current minimum visible level. */
vmav_log_level_t vmav_log_get_level(void);

/* For VMAV_LOG_SINK_FILE, set the file handle the logger writes to.
 * Must be called before the first log call after switching to the FILE
 * sink. Ownership stays with the caller; the logger does not fclose. */
void vmav_log_set_file(FILE *fp);

/* Emit a log entry. Prefer the level-specific macros below — they
 * capture __FILE__ and __LINE__ automatically. Entries below the
 * configured level are dropped. */
void vmav_logf(vmav_log_level_t level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define VMAV_LOG_TRACE(...) vmav_logf(VMAV_LL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define VMAV_LOG_DEBUG(...) vmav_logf(VMAV_LL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define VMAV_LOG_INFO(...) vmav_logf(VMAV_LL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define VMAV_LOG_WARN(...) vmav_logf(VMAV_LL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define VMAV_LOG_ERROR(...) vmav_logf(VMAV_LL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

/* Lookup helpers. Return stable strings; never NULL. */
const char *vmav_log_level_str(vmav_log_level_t level);

/* Parse a level name (case-insensitive: "trace", "debug", "info",
 * "warn", "error"). Returns VMAV_ERR_BAD_ARG on unknown input. */
vmav_status_t vmav_log_level_from_str(const char *name, vmav_log_level_t *out);

#ifdef __cplusplus
}
#endif
