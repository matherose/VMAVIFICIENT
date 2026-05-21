/* Anchor declaration so this TU is non-empty on POSIX builds. */
typedef int vmav_os_win_anchor_t;

#if defined(_WIN32)

#include "vmavificient/vmav_os.h"

#include <io.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

/* === UTF-8 / UTF-16 conversion =============================== */

/* Convert UTF-8 NUL-terminated string into a fixed wide buffer.
 * Returns the wide-char length on success (incl. NUL), 0 on error. */
static int utf8_to_utf16(const char *src, wchar_t *out, int out_cap) {
    if (src == NULL || out == NULL || out_cap <= 0) {
        return 0;
    }
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, out, out_cap);
}

/* Convert UTF-16 NUL-terminated string into a fixed UTF-8 buffer.
 * Returns the byte length on success (incl. NUL), 0 on error. */
static int utf16_to_utf8(const wchar_t *src, char *out, int out_cap) {
    if (src == NULL || out == NULL || out_cap <= 0) {
        return 0;
    }
    return WideCharToMultiByte(CP_UTF8, 0, src, -1, out, out_cap, NULL, NULL);
}

/* === Environment ============================================== */

/* Thread-local return buffer for vmav_env_get. The returned const
 * char* is valid until the next call from the same thread. */
static _Thread_local char tls_env_buf[VMAV_PATH_MAX];

const char *vmav_env_get(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    wchar_t wname[256];
    if (utf8_to_utf16(name, wname, (int)(sizeof(wname) / sizeof(wname[0]))) == 0) {
        return NULL;
    }
    wchar_t wval[VMAV_PATH_MAX];
    const DWORD n = GetEnvironmentVariableW(wname, wval, VMAV_PATH_MAX);
    if (n == 0 || n >= VMAV_PATH_MAX) {
        /* 0 = not set or empty; >= cap = truncated. We don't try to
         * grow the buffer — env vars longer than VMAV_PATH_MAX are
         * pathological for paths and not worth optimizing for. */
        return NULL;
    }
    if (utf16_to_utf8(wval, tls_env_buf, (int)sizeof(tls_env_buf)) == 0) {
        return NULL;
    }
    return tls_env_buf;
}

/* === Terminal ================================================= */

bool vmav_term_isatty(int fd) {
    return _isatty(fd) != 0;
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
    /* Best-effort: enable VT escape interpretation on stdout + stderr.
     * Available since Windows 10 1511 (build 10586). On older
     * systems GetConsoleMode succeeds but SetConsoleMode with the
     * VT flag returns 0; we still return OK and let ANSI escapes
     * print as garbage. The caller can detect this via NO_COLOR. */
    const DWORD vt_flag = 0x0004; /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
    HANDLE handles[2] = {GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
    for (int i = 0; i < 2; i++) {
        if (handles[i] == INVALID_HANDLE_VALUE || handles[i] == NULL) {
            continue;
        }
        DWORD mode = 0;
        if (GetConsoleMode(handles[i], &mode)) {
            (void)SetConsoleMode(handles[i], mode | vt_flag);
        }
    }
    return VMAV_OK_STATUS;
}

/* === Filesystem =============================================== */

bool vmav_fs_exists(const char *path) {
    if (path == NULL) {
        return false;
    }
    wchar_t wpath[VMAV_PATH_MAX];
    if (utf8_to_utf16(path, wpath, VMAV_PATH_MAX) == 0) {
        return false;
    }
    return GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES;
}

bool vmav_fs_is_dir(const char *path) {
    if (path == NULL) {
        return false;
    }
    wchar_t wpath[VMAV_PATH_MAX];
    if (utf8_to_utf16(path, wpath, VMAV_PATH_MAX) == 0) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static vmav_status_t mkdir_one_w(const wchar_t *wpath) {
    if (CreateDirectoryW(wpath, NULL)) {
        return VMAV_OK_STATUS;
    }
    const DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        /* Verify it's actually a directory. */
        const DWORD attrs = GetFileAttributesW(wpath);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return VMAV_OK_STATUS;
        }
        return VMAV_ERR(VMAV_ERR_IO, "path exists but is not a directory");
    }
    return VMAV_ERR(VMAV_ERR_IO, "CreateDirectoryW: error %lu", (unsigned long)err);
}

vmav_status_t vmav_fs_mkdir_p(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_fs_mkdir_p: null/empty path");
    }
    wchar_t wpath[VMAV_PATH_MAX];
    if (utf8_to_utf16(path, wpath, VMAV_PATH_MAX) == 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_fs_mkdir_p: utf-8 conversion failed");
    }

    /* Walk components, creating each as we go. Accept both / and \
     * as separators. Skip the drive prefix "C:" if present. */
    const size_t n = wcslen(wpath);
    size_t start = 0;
    if (n >= 2 && wpath[1] == L':') {
        start = 2; /* skip "C:" */
        if (n >= 3 && (wpath[2] == L'\\' || wpath[2] == L'/')) {
            start = 3;
        }
    } else if (n >= 1 && (wpath[0] == L'\\' || wpath[0] == L'/')) {
        start = 1;
    }

    for (size_t i = start; i < n; i++) {
        if (wpath[i] == L'\\' || wpath[i] == L'/') {
            const wchar_t saved = wpath[i];
            wpath[i] = L'\0';
            vmav_status_t st = mkdir_one_w(wpath);
            wpath[i] = saved;
            if (!vmav_status_ok(st)) {
                return st;
            }
        }
    }
    return mkdir_one_w(wpath);
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

    /* On Windows, paths can use either separator. Treat both as
     * boundary chars; emit forward slashes for consistency (Win32
     * file APIs accept '/'). Drive-letter absolute (C:\...) and
     * UNC (\\server\share) are also "absolute" — treat b as
     * absolute if it begins with '/', '\\', or a drive letter. */
    const bool b_absolute =
        (lb > 0 && (b[0] == '/' || b[0] == '\\')) ||
        (lb >= 2 &&
         (((b[0] >= 'A' && b[0] <= 'Z') || (b[0] >= 'a' && b[0] <= 'z')) && b[1] == ':'));

    size_t la_eff = la;
    while (la_eff > 1 && (a[la_eff - 1] == '/' || a[la_eff - 1] == '\\')) {
        --la_eff;
    }
    size_t b_off = 0;
    while (b_off < lb && (b[b_off] == '/' || b[b_off] == '\\')) {
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

static vmav_status_t
known_folder_path(const KNOWNFOLDERID *id, const char *subdir, char *out, size_t out_size) {
    PWSTR wfolder = NULL;
    HRESULT hr = SHGetKnownFolderPath(id, 0, NULL, &wfolder);
    if (FAILED(hr) || wfolder == NULL) {
        if (wfolder != NULL) {
            CoTaskMemFree(wfolder);
        }
        return VMAV_ERR(
            VMAV_ERR_NOT_FOUND, "SHGetKnownFolderPath failed: 0x%lx", (unsigned long)hr);
    }
    char folder_utf8[VMAV_PATH_MAX];
    const int n = utf16_to_utf8(wfolder, folder_utf8, (int)sizeof(folder_utf8));
    CoTaskMemFree(wfolder);
    if (n == 0) {
        return VMAV_ERR(VMAV_ERR_PARSE, "known-folder utf-16->utf-8 conversion failed");
    }
    return vmav_path_join(out, out_size, folder_utf8, subdir);
}

vmav_status_t vmav_path_config_dir(char *out, size_t out_size) {
    return known_folder_path(&FOLDERID_RoamingAppData, "vmavificient", out, out_size);
}

vmav_status_t vmav_path_cache_dir(char *out, size_t out_size) {
    return known_folder_path(&FOLDERID_LocalAppData, "vmavificient/Cache", out, out_size);
}

/* === Time ===================================================== */

uint64_t vmav_time_now_ms(void) {
    LARGE_INTEGER counter;
    LARGE_INTEGER freq;
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&freq) ||
        freq.QuadPart == 0) {
        return (uint64_t)GetTickCount64();
    }
    return (uint64_t)((counter.QuadPart * 1000LL) / freq.QuadPart);
}

vmav_status_t vmav_time_now_iso8601(char *out, size_t out_size) {
    if (out == NULL || out_size < 21) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "iso8601 buffer too small (need >=21)");
    }
    SYSTEMTIME st;
    GetSystemTime(&st);
    const int n = snprintf(out,
                           out_size,
                           "%04u-%02u-%02uT%02u:%02u:%02uZ",
                           (unsigned)st.wYear,
                           (unsigned)st.wMonth,
                           (unsigned)st.wDay,
                           (unsigned)st.wHour,
                           (unsigned)st.wMinute,
                           (unsigned)st.wSecond);
    if (n < 0 || (size_t)n >= out_size) {
        return VMAV_ERR(VMAV_ERR_IO, "iso8601 snprintf truncated");
    }
    return VMAV_OK_STATUS;
}

#endif /* _WIN32 */
