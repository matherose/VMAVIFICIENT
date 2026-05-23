/* Final MKV mux: shells out to the ffmpeg CLI via vmav_subproc to
 * combine the AV1 IVF + opus audio files + SRT subtitles into the
 * output .mkv. Uses `-c copy` so encoded streams pass through
 * byte-identical; metadata (language / title / dispositions) is
 * applied per-track via `-metadata:s:<type>:<idx>` and
 * `-disposition:<type>:<idx>` flags. */

#include "vmavificient/vmav_final_mux.h"
#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_subproc.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

/* Simple argv builder. argv strings are owned by the builder via a
 * single heap pool; freed by argv_builder_free. */
typedef struct {
    const char **argv;
    size_t count;
    size_t cap;
    char *pool; /* arena for owned strings */
    size_t pool_used;
    size_t pool_cap;
} argv_builder_t;

static void argv_builder_init(argv_builder_t *b) {
    memset(b, 0, sizeof(*b));
}

static void argv_builder_free(argv_builder_t *b) {
    free(b->argv);
    free(b->pool);
    memset(b, 0, sizeof(*b));
}

/* Push a literal (already-NUL-terminated, caller-lifetime) argv. */
static bool argv_push(argv_builder_t *b, const char *s) {
    if (b->count + 1 >= b->cap) {
        const size_t new_cap = b->cap == 0 ? 64 : b->cap * 2;
        const char **resized = realloc(b->argv, new_cap * sizeof(*resized));
        if (resized == NULL) {
            return false;
        }
        b->argv = resized;
        b->cap = new_cap;
    }
    b->argv[b->count++] = s;
    b->argv[b->count] = NULL; /* keep NULL-terminated */
    return true;
}

/* Push a printf-formatted argv. Owned by the builder. */
__attribute__((format(printf, 2, 3))) static bool
argv_pushf(argv_builder_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    /* Probe size. */
    va_list ap2;
    va_copy(ap2, ap);
    const int needed = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (needed < 0) {
        va_end(ap);
        return false;
    }
    const size_t need = (size_t)needed + 1;
    if (b->pool_used + need > b->pool_cap) {
        const size_t new_cap = (b->pool_used + need > b->pool_cap * 2)
                                   ? (b->pool_used + need + 1024)
                                   : (b->pool_cap * 2 + 1024);
        char *resized = realloc(b->pool, new_cap);
        if (resized == NULL) {
            va_end(ap);
            return false;
        }
        /* The realloc may have moved the buffer. We need to rebase
         * existing argv pointers that point into the old pool. Easy
         * sentinel: pointers between b->pool and b->pool + b->pool_used. */
        if (resized != b->pool) {
            for (size_t i = 0; i < b->count; i++) {
                const char *p = b->argv[i];
                if (p >= b->pool && p < b->pool + b->pool_used) {
                    b->argv[i] = resized + (p - b->pool);
                }
            }
        }
        b->pool = resized;
        b->pool_cap = new_cap;
    }
    char *dst = b->pool + b->pool_used;
    (void)vsnprintf(dst, need, fmt, ap);
    va_end(ap);
    b->pool_used += need;
    return argv_push(b, dst);
}

vmav_status_t vmav_final_mux(const vmav_final_mux_spec_t *spec, vmav_final_mux_result_t *out) {
    if (spec == NULL || out == NULL || spec->video_path == NULL || spec->output_path == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_final_mux: null arg");
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->output_path, sizeof(out->output_path), "%s", spec->output_path);

    struct stat st;
    if (stat(spec->output_path, &st) == 0 && st.st_size > 0) {
        out->skipped = true;
        VMAV_LOG_INFO("final_mux: '%s' already exists (%lld bytes), skipping",
                      spec->output_path,
                      (long long)st.st_size);
        return VMAV_OK_STATUS;
    }

    argv_builder_t b;
    argv_builder_init(&b);
    vmav_status_t status = VMAV_OK_STATUS;

#define PUSH(s)                                                                                    \
    do {                                                                                           \
        if (!argv_push(&b, (s))) {                                                                 \
            status = VMAV_ERR(VMAV_ERR_NO_MEM, "argv_push");                                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
#define PUSHF(...)                                                                                 \
    do {                                                                                           \
        if (!argv_pushf(&b, __VA_ARGS__)) {                                                        \
            status = VMAV_ERR(VMAV_ERR_NO_MEM, "argv_pushf");                                      \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)

    PUSH("ffmpeg");
    PUSH("-y");
    PUSH("-loglevel");
    PUSH("error");
    PUSH("-nostdin");

    /* Inputs: video first, then audio, then subtitles, then optional chapters. */
    PUSH("-i");
    PUSH(spec->video_path);
    for (size_t i = 0; i < spec->audio_count; i++) {
        PUSH("-i");
        PUSH(spec->audio[i].path);
    }
    for (size_t i = 0; i < spec->sub_count; i++) {
        PUSH("-i");
        PUSH(spec->subs[i].path);
    }
    int chapters_idx = -1;
    if (spec->chapters_source != NULL && spec->chapters_source[0] != '\0') {
        chapters_idx = 1 + (int)spec->audio_count + (int)spec->sub_count;
        PUSH("-i");
        PUSH(spec->chapters_source);
    }

    /* Stream maps. */
    PUSH("-map");
    PUSH("0:v:0");
    for (size_t i = 0; i < spec->audio_count; i++) {
        PUSH("-map");
        PUSHF("%zu:a:0", 1 + i);
    }
    for (size_t i = 0; i < spec->sub_count; i++) {
        PUSH("-map");
        PUSHF("%zu:s:0?", 1 + spec->audio_count + i);
    }
    PUSH("-map_chapters");
    if (chapters_idx >= 0) {
        PUSHF("%d", chapters_idx);
    } else {
        PUSH("-1");
    }

    PUSH("-c");
    PUSH("copy");
    PUSH("-c:s");
    PUSH("srt");

    /* Clear video dispositions we don't own. */
    PUSH("-disposition:v:0");
    PUSH("0");

    if (spec->video_title != NULL && spec->video_title[0] != '\0') {
        PUSH("-metadata:s:v:0");
        PUSHF("title=%s", spec->video_title);
    }
    if (spec->video_language != NULL && spec->video_language[0] != '\0') {
        PUSH("-metadata:s:v:0");
        PUSHF("language=%s", spec->video_language);
    }

    /* Audio metadata + dispositions. */
    for (size_t i = 0; i < spec->audio_count; i++) {
        const char *lang = (spec->audio[i].language != NULL && spec->audio[i].language[0] != '\0')
                               ? spec->audio[i].language
                               : "und";
        PUSHF("-metadata:s:a:%zu", i);
        PUSHF("language=%s", lang);
        if (spec->audio[i].track_name != NULL && spec->audio[i].track_name[0] != '\0') {
            PUSHF("-metadata:s:a:%zu", i);
            PUSHF("title=%s", spec->audio[i].track_name);
        }
        PUSHF("-disposition:a:%zu", i);
        PUSH(spec->audio[i].is_default ? "default" : "0");
    }

    /* Subtitle metadata + dispositions. */
    for (size_t i = 0; i < spec->sub_count; i++) {
        const char *lang = (spec->subs[i].language != NULL && spec->subs[i].language[0] != '\0')
                               ? spec->subs[i].language
                               : "und";
        PUSHF("-metadata:s:s:%zu", i);
        PUSHF("language=%s", lang);
        if (spec->subs[i].track_name != NULL && spec->subs[i].track_name[0] != '\0') {
            PUSHF("-metadata:s:s:%zu", i);
            PUSHF("title=%s", spec->subs[i].track_name);
        }
        /* Compose disposition string. */
        char disp[96] = {0};
        size_t dlen = 0;
        bool any = false;
#define APPEND_DISP(flag)                                                                          \
    do {                                                                                           \
        const size_t step =                                                                        \
            (size_t)snprintf(disp + dlen, sizeof(disp) - dlen, "%s%s", any ? "+" : "", flag);      \
        dlen += step;                                                                              \
        any = true;                                                                                \
    } while (0)
        if (spec->subs[i].is_default) {
            APPEND_DISP("default");
        }
        if (spec->subs[i].is_forced) {
            APPEND_DISP("forced");
        }
        if (spec->subs[i].is_sdh) {
            APPEND_DISP("hearing_impaired");
        }
#undef APPEND_DISP
        PUSHF("-disposition:s:%zu", i);
        PUSH(any ? disp : "0");
    }

    /* Container title. */
    if (spec->container_title != NULL && spec->container_title[0] != '\0') {
        PUSH("-metadata");
        PUSHF("title=%s", spec->container_title);
    }

    /* Matroska: don't auto-mark a subtitle as default when none is tagged. */
    PUSH("-default_mode");
    PUSH("infer_no_subs");

    /* Output. */
    PUSH(spec->output_path);

#undef PUSH
#undef PUSHF

    vmav_subproc_spec_t sp = {
        .exe = "ffmpeg",
        .argv = b.argv,
        .capture_stderr = true,
    };
    vmav_subproc_result_t sr = {0};
    status = vmav_subproc_run(&sp, &sr);
    if (vmav_status_ok(status) && sr.exit_code != 0) {
        const char *stderr_msg =
            (sr.stderr_buf.data != NULL) ? (const char *)sr.stderr_buf.data : "";
        status = VMAV_ERR(VMAV_ERR_SUBPROC, "ffmpeg exit %d: %s", sr.exit_code, stderr_msg);
        remove(spec->output_path);
    }
    vmav_subproc_result_free(&sr);

cleanup:
    argv_builder_free(&b);
    return status;
}
