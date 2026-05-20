#include "vmavificient/vmav_log.h"

#include "vmavificient/vmav_os.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* === State ==================================================== */

static struct {
    vmav_log_level_t level;
    vmav_log_sink_t  sink;
    FILE            *file_fp;
    bool             initialized;
} g_log = {VMAV_LL_INFO, VMAV_LOG_SINK_STDERR, NULL, false};

/* === Lookup =================================================== */

const char *vmav_log_level_str(vmav_log_level_t level) {
    switch (level) {
        case VMAV_LL_TRACE: return "TRACE";
        case VMAV_LL_DEBUG: return "DEBUG";
        case VMAV_LL_INFO:  return "INFO";
        case VMAV_LL_WARN:  return "WARN";
        case VMAV_LL_ERROR: return "ERROR";
    }
    return "?";
}

vmav_status_t vmav_log_level_from_str(const char *name, vmav_log_level_t *out) {
    if (name == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_log_level_from_str: null arg");
    }
    if (strcasecmp(name, "trace") == 0) { *out = VMAV_LL_TRACE; return VMAV_OK_STATUS; }
    if (strcasecmp(name, "debug") == 0) { *out = VMAV_LL_DEBUG; return VMAV_OK_STATUS; }
    if (strcasecmp(name, "info")  == 0) { *out = VMAV_LL_INFO;  return VMAV_OK_STATUS; }
    if (strcasecmp(name, "warn")  == 0) { *out = VMAV_LL_WARN;  return VMAV_OK_STATUS; }
    if (strcasecmp(name, "error") == 0) { *out = VMAV_LL_ERROR; return VMAV_OK_STATUS; }
    return VMAV_ERR(VMAV_ERR_BAD_ARG, "unknown log level '%s'", name);
}

/* === Config =================================================== */

void vmav_log_init(vmav_log_level_t level, vmav_log_sink_t sink) {
    g_log.level       = level;
    g_log.sink        = sink;
    g_log.initialized = true;
}

void vmav_log_set_level(vmav_log_level_t level) {
    g_log.level = level;
}

vmav_log_level_t vmav_log_get_level(void) {
    return g_log.level;
}

void vmav_log_set_file(FILE *fp) {
    g_log.file_fp = fp;
}

/* === Emission ================================================= */

static const char *level_color(vmav_log_level_t level) {
    switch (level) {
        case VMAV_LL_TRACE: return "\x1b[90m"; /* bright black */
        case VMAV_LL_DEBUG: return "\x1b[36m"; /* cyan */
        case VMAV_LL_INFO:  return "\x1b[32m"; /* green */
        case VMAV_LL_WARN:  return "\x1b[33m"; /* yellow */
        case VMAV_LL_ERROR: return "\x1b[31m"; /* red */
    }
    return "";
}

static void write_json_escaped(FILE *fp, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p) {
        switch (*p) {
            case '"':  fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\n': fputs("\\n",  fp); break;
            case '\r': fputs("\\r",  fp); break;
            case '\t': fputs("\\t",  fp); break;
            default:
                if (*p < 0x20) {
                    fprintf(fp, "\\u%04x", *p);
                } else {
                    fputc((int)*p, fp);
                }
                break;
        }
    }
}

void vmav_logf(vmav_log_level_t level,
               const char *file,
               int line,
               const char *fmt,
               ...) {
    if (!g_log.initialized) {
        vmav_log_init(g_log.level, g_log.sink);
    }
    if ((int)level < (int)g_log.level || g_log.sink == VMAV_LOG_SINK_NONE) {
        return;
    }

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char ts[24];
    if (!vmav_status_ok(vmav_time_now_iso8601(ts, sizeof(ts)))) {
        ts[0] = '\0';
    }

    /* Strip source-dir prefix if vmav_compile_options set
     * -ffile-prefix-map; file looks like "vmav/src/...". Otherwise
     * keep as-is. */
    const char *short_file = file;
    if (file != NULL) {
        const char *slash = strrchr(file, '/');
        if (slash != NULL) {
            short_file = slash + 1;
        }
    }

    if (g_log.sink == VMAV_LOG_SINK_FILE) {
        FILE *fp = g_log.file_fp != NULL ? g_log.file_fp : stderr;
        fprintf(fp, "%s [%s] %s:%d  %s\n",
                ts, vmav_log_level_str(level),
                short_file != NULL ? short_file : "?", line, msg);
        fflush(fp);
        return;
    }

    if (g_log.sink == VMAV_LOG_SINK_JSON_LINES) {
        FILE *fp = stderr;
        fprintf(fp, "{\"ts\":\"%s\",\"level\":\"%s\",\"file\":\"",
                ts, vmav_log_level_str(level));
        write_json_escaped(fp, short_file != NULL ? short_file : "");
        fprintf(fp, "\",\"line\":%d,\"msg\":\"", line);
        write_json_escaped(fp, msg);
        fputs("\"}\n", fp);
        fflush(fp);
        return;
    }

    /* Default sink: stderr, colored when the destination is a TTY
     * and the user hasn't disabled color. */
    FILE *fp = stderr;
    const bool use_color = vmav_term_isatty(2) && !vmav_term_no_color();
    if (use_color) {
        fprintf(fp, "%s[%s]\x1b[0m %s:%d  %s\n",
                level_color(level), vmav_log_level_str(level),
                short_file != NULL ? short_file : "?", line, msg);
    } else {
        fprintf(fp, "[%s] %s:%d  %s\n",
                vmav_log_level_str(level),
                short_file != NULL ? short_file : "?", line, msg);
    }
    fflush(fp);
}
