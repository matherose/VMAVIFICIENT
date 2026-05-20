/* Anchor declaration so this TU is non-empty on Windows builds. */
typedef int vmav_os_posix_anchor_t;

#if defined(_WIN32)
/* Windows impl lives in vmav_os_win.c (Phase 2). */
#else

#include "vmavificient/vmav_os.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

/* === Environment ============================================== */

const char *vmav_env_get(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    return getenv(name);
}

/* === Terminal ================================================= */

bool vmav_term_isatty(int fd) {
    return isatty(fd) != 0;
}

bool vmav_term_no_color(void) {
    const char *no_color = vmav_env_get("NO_COLOR");
    if (no_color != NULL && no_color[0] != '\0') {
        return true;
    }
    const char *term = vmav_env_get("TERM");
    if (term != NULL && strcmp(term, "dumb") == 0) {
        return true;
    }
    return false;
}

vmav_status_t vmav_term_enable_vt(void) {
    /* POSIX terminals are already VT-capable. */
    return VMAV_OK_STATUS;
}

/* === Filesystem =============================================== */

bool vmav_fs_exists(const char *path) {
    if (path == NULL) {
        return false;
    }
    struct stat sb;
    return stat(path, &sb) == 0;
}

bool vmav_fs_is_dir(const char *path) {
    if (path == NULL) {
        return false;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return false;
    }
    return S_ISDIR(sb.st_mode);
}

static vmav_status_t mkdir_one(const char *path) {
    if (mkdir(path, 0755) == 0) {
        return VMAV_OK_STATUS;
    }
    if (errno == EEXIST) {
        /* Existing path must be a directory, otherwise fail. */
        if (vmav_fs_is_dir(path)) {
            return VMAV_OK_STATUS;
        }
        return VMAV_ERR(VMAV_ERR_IO, "path '%s' exists but is not a directory", path);
    }
    return VMAV_ERR(VMAV_ERR_IO, "mkdir('%s'): %s", path, strerror(errno));
}

vmav_status_t vmav_fs_mkdir_p(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_fs_mkdir_p: null/empty path");
    }
    char buf[VMAV_PATH_MAX];
    const size_t n = strlen(path);
    if (n + 1 > sizeof(buf)) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "path too long (%zu bytes)", n);
    }
    memcpy(buf, path, n + 1);

    /* Walk components, creating each as we go. Skip the leading '/'. */
    for (size_t i = 1; i < n; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            vmav_status_t st = mkdir_one(buf);
            if (!vmav_status_ok(st)) {
                return st;
            }
            buf[i] = '/';
        }
    }
    return mkdir_one(buf);
}

/* === Paths ==================================================== */

vmav_status_t vmav_path_join(char *out, size_t out_size, const char *a, const char *b) {
    if (out == NULL || out_size == 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_path_join: null out buffer");
    }
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    const size_t la = strlen(a);
    const size_t lb = strlen(b);

    /* If b is absolute, ignore a entirely. */
    const bool b_absolute = (lb > 0 && b[0] == '/');
    /* Strip trailing slash on a (unless a is exactly "/"). */
    size_t la_eff = la;
    while (la_eff > 1 && a[la_eff - 1] == '/') {
        --la_eff;
    }
    /* Strip leading slash on b. */
    size_t b_off = 0;
    while (b_off < lb && b[b_off] == '/') {
        ++b_off;
    }

    int written;
    if (b_absolute || la == 0) {
        written = snprintf(out, out_size, "%s", b);
    } else if (lb == 0) {
        written = snprintf(out, out_size, "%.*s", (int)la_eff, a);
    } else {
        written = snprintf(out, out_size, "%.*s/%s", (int)la_eff, a, b + b_off);
    }
    if (written < 0 || (size_t)written >= out_size) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG,
                        "vmav_path_join: result truncated (need %d bytes, have %zu)",
                        written,
                        out_size);
    }
    return VMAV_OK_STATUS;
}

#if defined(__APPLE__)
#define VMAV_OS_MACOS 1
#else
#define VMAV_OS_LINUX 1
#endif

static vmav_status_t home_dir(char *out, size_t out_size) {
    const char *home = vmav_env_get("HOME");
    if (home == NULL || home[0] == '\0') {
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "HOME is not set");
    }
    const size_t n = strlen(home);
    if (n + 1 > out_size) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "HOME path too long");
    }
    memcpy(out, home, n + 1);
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_path_config_dir(char *out, size_t out_size) {
#if defined(VMAV_OS_MACOS)
    char home[VMAV_PATH_MAX];
    VMAV_TRY(home_dir(home, sizeof(home)));
    return vmav_path_join(out, out_size, home, "Library/Application Support/vmavificient");
#else
    const char *xdg = vmav_env_get("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        return vmav_path_join(out, out_size, xdg, "vmavificient");
    }
    char home[VMAV_PATH_MAX];
    VMAV_TRY(home_dir(home, sizeof(home)));
    char dotconfig[VMAV_PATH_MAX];
    VMAV_TRY(vmav_path_join(dotconfig, sizeof(dotconfig), home, ".config"));
    return vmav_path_join(out, out_size, dotconfig, "vmavificient");
#endif
}

vmav_status_t vmav_path_cache_dir(char *out, size_t out_size) {
#if defined(VMAV_OS_MACOS)
    char home[VMAV_PATH_MAX];
    VMAV_TRY(home_dir(home, sizeof(home)));
    return vmav_path_join(out, out_size, home, "Library/Caches/vmavificient");
#else
    const char *xdg = vmav_env_get("XDG_CACHE_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        return vmav_path_join(out, out_size, xdg, "vmavificient");
    }
    char home[VMAV_PATH_MAX];
    VMAV_TRY(home_dir(home, sizeof(home)));
    char dotcache[VMAV_PATH_MAX];
    VMAV_TRY(vmav_path_join(dotcache, sizeof(dotcache), home, ".cache"));
    return vmav_path_join(out, out_size, dotcache, "vmavificient");
#endif
}

/* === Time ===================================================== */

uint64_t vmav_time_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

vmav_status_t vmav_time_now_iso8601(char *out, size_t out_size) {
    if (out == NULL || out_size < 21) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "iso8601 buffer too small (need >=21)");
    }
    time_t now = time(NULL);
    struct tm tm_utc;
    if (gmtime_r(&now, &tm_utc) == NULL) {
        return VMAV_ERR(VMAV_ERR_IO, "gmtime_r failed");
    }
    if (strftime(out, out_size, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0) {
        return VMAV_ERR(VMAV_ERR_IO, "strftime failed");
    }
    return VMAV_OK_STATUS;
}

#endif /* !_WIN32 */
