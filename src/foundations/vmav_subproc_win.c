/* Anchor declaration so this TU is non-empty on POSIX builds. */
typedef int vmav_subproc_win_anchor_t;

#if defined(_WIN32)

#include "vmavificient/vmav_os.h"
#include "vmavificient/vmav_subproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#define VMAV_SUBPROC_BUF_INITIAL 4096
#define VMAV_SUBPROC_BUF_MAX (16U * 1024U * 1024U)
#define VMAV_SUBPROC_CMDLINE_MAX 32768 /* Win32 CreateProcessW limit */

/* === Buffer helpers (mirror of POSIX impl) ==================== */

static vmav_status_t buf_append(vmav_buf_t *b, const char *data, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t new_cap = b->cap > 0 ? b->cap : (size_t)VMAV_SUBPROC_BUF_INITIAL;
        while (new_cap < b->len + n + 1) {
            new_cap *= 2;
            if (new_cap > VMAV_SUBPROC_BUF_MAX) {
                return VMAV_ERR(VMAV_ERR_SUBPROC,
                                "captured output exceeds %u-byte cap",
                                (unsigned)VMAV_SUBPROC_BUF_MAX);
            }
        }
        char *p = realloc(b->data, new_cap);
        if (p == NULL) {
            return VMAV_ERR(VMAV_ERR_NO_MEM, "subproc buf realloc(%zu)", new_cap);
        }
        b->data = p;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, data, n);
    b->len += n;
    b->data[b->len] = '\0';
    return VMAV_OK_STATUS;
}

void vmav_subproc_result_free(vmav_subproc_result_t *out) {
    if (out == NULL) {
        return;
    }
    free(out->stdout_buf.data);
    free(out->stderr_buf.data);
    memset(&out->stdout_buf, 0, sizeof(out->stdout_buf));
    memset(&out->stderr_buf, 0, sizeof(out->stderr_buf));
}

/* === UTF-8 → UTF-16 ========================================== */

static int utf8_to_utf16(const char *src, wchar_t *out, int out_cap) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, out, out_cap);
}

/* === Win32 argv quoting =====================================
 *
 * Implements the algorithm documented by Microsoft for argv reconstruction
 * (the one CommandLineToArgvW expects). For each argument:
 *   - If it contains no whitespace, no quotes, and is non-empty, append as-is.
 *   - Otherwise wrap it in quotes; double any backslashes immediately
 *     preceding a quote or the closing quote; escape embedded quotes
 *     with a backslash. */

static bool needs_quoting(const wchar_t *s) {
    if (*s == L'\0') {
        return true;
    }
    for (; *s; s++) {
        if (*s == L' ' || *s == L'\t' || *s == L'\v' || *s == L'\n' || *s == L'\r' || *s == L'"') {
            return true;
        }
    }
    return false;
}

static vmav_status_t
cmdline_append_arg(wchar_t *cmd, size_t cmd_cap, size_t *len, const wchar_t *arg) {
    if (*len > 0) {
        if (*len + 1 >= cmd_cap) {
            return VMAV_ERR(VMAV_ERR_SUBPROC, "command line truncated");
        }
        cmd[(*len)++] = L' ';
    }
    if (!needs_quoting(arg)) {
        const size_t alen = wcslen(arg);
        if (*len + alen >= cmd_cap) {
            return VMAV_ERR(VMAV_ERR_SUBPROC, "command line truncated");
        }
        memcpy(cmd + *len, arg, alen * sizeof(wchar_t));
        *len += alen;
        return VMAV_OK_STATUS;
    }

    if (*len + 1 >= cmd_cap) {
        return VMAV_ERR(VMAV_ERR_SUBPROC, "command line truncated");
    }
    cmd[(*len)++] = L'"';

    while (*arg) {
        size_t bs_run = 0;
        while (*arg == L'\\') {
            bs_run++;
            arg++;
        }
        const size_t bs_to_emit =
            (*arg == L'\0') ? bs_run * 2 : (*arg == L'"' ? bs_run * 2 + 1 : bs_run);
        if (*len + bs_to_emit + 1 >= cmd_cap) {
            return VMAV_ERR(VMAV_ERR_SUBPROC, "command line truncated");
        }
        for (size_t i = 0; i < bs_to_emit; i++) {
            cmd[(*len)++] = L'\\';
        }
        if (*arg == L'\0') {
            break;
        }
        cmd[(*len)++] = *arg++;
    }
    if (*len + 1 >= cmd_cap) {
        return VMAV_ERR(VMAV_ERR_SUBPROC, "command line truncated");
    }
    cmd[(*len)++] = L'"';
    return VMAV_OK_STATUS;
}

/* === Reader thread =========================================== */

typedef struct {
    HANDLE pipe;
    vmav_buf_t *buf;
    vmav_status_t status;
} reader_ctx_t;

static DWORD WINAPI reader_thread_proc(LPVOID arg) {
    reader_ctx_t *ctx = (reader_ctx_t *)arg;
    char tmp[8192];
    DWORD nread = 0;
    while (ReadFile(ctx->pipe, tmp, (DWORD)sizeof(tmp), &nread, NULL)) {
        if (nread == 0) {
            break;
        }
        vmav_status_t st = buf_append(ctx->buf, tmp, (size_t)nread);
        if (!vmav_status_ok(st)) {
            ctx->status = st;
            return 1U;
        }
    }
    /* ReadFile returns FALSE with ERROR_BROKEN_PIPE when the child
     * closes its end normally. Any other error is real. */
    const DWORD err = GetLastError();
    if (err != ERROR_BROKEN_PIPE && err != 0) {
        ctx->status = VMAV_ERR(VMAV_ERR_SUBPROC, "ReadFile: error %lu", (unsigned long)err);
        return 1U;
    }
    return 0U;
}

/* === Main entry point ======================================== */

vmav_status_t vmav_subproc_run(const vmav_subproc_spec_t *spec, vmav_subproc_result_t *out) {
    if (spec == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_subproc_run: null arg");
    }
    if (spec->exe == NULL || spec->argv == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_subproc_run: null exe/argv");
    }
    if (spec->cwd != NULL) {
        return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_subproc: spec->cwd not supported");
    }
    if (spec->envp != NULL) {
        return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_subproc: spec->envp not supported on Win");
    }

    memset(out, 0, sizeof(*out));

    HANDLE stdout_rd = NULL;
    HANDLE stdout_wr = NULL;
    HANDLE stderr_rd = NULL;
    HANDLE stderr_wr = NULL;
    HANDLE nul = INVALID_HANDLE_VALUE;
    HANDLE thread_so = NULL;
    HANDLE thread_se = NULL;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    bool proc_inited = false;
    vmav_status_t st = VMAV_OK_STATUS;

    /* Pipes inherit-able by the child. The parent's end is then
     * de-inherited via SetHandleInformation. */
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (spec->capture_stdout) {
        if (!CreatePipe(&stdout_rd, &stdout_wr, &sa, 0)) {
            st = VMAV_ERR(VMAV_ERR_SUBPROC, "CreatePipe(stdout) failed");
            goto cleanup;
        }
        SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);
    }
    if (spec->capture_stderr) {
        if (!CreatePipe(&stderr_rd, &stderr_wr, &sa, 0)) {
            st = VMAV_ERR(VMAV_ERR_SUBPROC, "CreatePipe(stderr) failed");
            goto cleanup;
        }
        SetHandleInformation(stderr_rd, HANDLE_FLAG_INHERIT, 0);
    }

    nul = CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (nul == INVALID_HANDLE_VALUE) {
        st = VMAV_ERR(VMAV_ERR_IO, "CreateFile(NUL) failed: %lu", GetLastError());
        goto cleanup;
    }

    /* Build the UTF-16 command line by quoting each argv entry. */
    wchar_t *cmdline = malloc(VMAV_SUBPROC_CMDLINE_MAX * sizeof(wchar_t));
    if (cmdline == NULL) {
        st = VMAV_ERR(VMAV_ERR_NO_MEM, "cmdline malloc");
        goto cleanup;
    }
    cmdline[0] = L'\0';
    size_t c_len = 0;
    for (const char *const *p = spec->argv; *p != NULL; p++) {
        wchar_t warg[VMAV_PATH_MAX];
        if (utf8_to_utf16(*p, warg, VMAV_PATH_MAX) == 0) {
            st = VMAV_ERR(VMAV_ERR_BAD_ARG, "argv element utf-8 invalid: %s", *p);
            free(cmdline);
            goto cleanup;
        }
        st = cmdline_append_arg(cmdline, VMAV_SUBPROC_CMDLINE_MAX, &c_len, warg);
        if (!vmav_status_ok(st)) {
            free(cmdline);
            goto cleanup;
        }
    }
    cmdline[c_len] = L'\0';

    wchar_t wexe[VMAV_PATH_MAX];
    const bool exe_has_slash = strchr(spec->exe, '/') != NULL || strchr(spec->exe, '\\') != NULL;
    if (utf8_to_utf16(spec->exe, wexe, VMAV_PATH_MAX) == 0) {
        st = VMAV_ERR(VMAV_ERR_BAD_ARG, "exe utf-8 invalid: %s", spec->exe);
        free(cmdline);
        goto cleanup;
    }

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = spec->capture_stdout ? stdout_wr : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = spec->capture_stderr ? stderr_wr : GetStdHandle(STD_ERROR_HANDLE);

    const uint64_t t_start = vmav_time_now_ms();
    const BOOL spawn_ok = CreateProcessW(
        /* lpApplicationName */ exe_has_slash ? wexe : NULL,
        /* lpCommandLine     */ cmdline,
        /* lpProcessAttrs    */ NULL,
        /* lpThreadAttrs     */ NULL,
        /* bInheritHandles   */ TRUE,
        /* dwCreationFlags   */ 0,
        /* lpEnvironment     */ NULL,
        /* lpCurrentDirectory*/ NULL,
        &si,
        &pi);
    free(cmdline);

    if (!spawn_ok) {
        st = VMAV_ERR(VMAV_ERR_SUBPROC,
                      "CreateProcessW('%s'): error %lu",
                      spec->exe,
                      (unsigned long)GetLastError());
        goto cleanup;
    }
    proc_inited = true;

    /* Parent closes the write ends so child's EOF on close propagates. */
    if (stdout_wr != NULL) {
        CloseHandle(stdout_wr);
        stdout_wr = NULL;
    }
    if (stderr_wr != NULL) {
        CloseHandle(stderr_wr);
        stderr_wr = NULL;
    }
    CloseHandle(nul);
    nul = INVALID_HANDLE_VALUE;

    /* Spawn reader threads (only for captured streams). */
    reader_ctx_t so_ctx = {.pipe = stdout_rd, .buf = &out->stdout_buf, .status = VMAV_OK_STATUS};
    reader_ctx_t se_ctx = {.pipe = stderr_rd, .buf = &out->stderr_buf, .status = VMAV_OK_STATUS};
    if (spec->capture_stdout) {
        thread_so = CreateThread(NULL, 0, reader_thread_proc, &so_ctx, 0, NULL);
        if (thread_so == NULL) {
            st = VMAV_ERR(VMAV_ERR_SUBPROC, "CreateThread(stdout) failed");
            TerminateProcess(pi.hProcess, 1);
            goto join;
        }
    }
    if (spec->capture_stderr) {
        thread_se = CreateThread(NULL, 0, reader_thread_proc, &se_ctx, 0, NULL);
        if (thread_se == NULL) {
            st = VMAV_ERR(VMAV_ERR_SUBPROC, "CreateThread(stderr) failed");
            TerminateProcess(pi.hProcess, 1);
            goto join;
        }
    }

    /* Wait for the process, applying timeout if requested. */
    {
        const DWORD wait_ms = (spec->timeout_ms == 0) ? INFINITE : (DWORD)spec->timeout_ms;
        const DWORD wait_rc = WaitForSingleObject(pi.hProcess, wait_ms);
        if (wait_rc == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            out->timed_out = true;
        } else if (wait_rc == WAIT_FAILED) {
            st = VMAV_ERR(
                VMAV_ERR_SUBPROC, "WaitForSingleObject: error %lu", (unsigned long)GetLastError());
            TerminateProcess(pi.hProcess, 1);
        }
    }

join:
    /* Reader threads exit when their pipes hit EOF (which happens when
     * the child exits or is terminated). Join them. */
    if (thread_so != NULL) {
        WaitForSingleObject(thread_so, INFINITE);
        if (!vmav_status_ok(so_ctx.status) && vmav_status_ok(st)) {
            st = so_ctx.status;
        }
    }
    if (thread_se != NULL) {
        WaitForSingleObject(thread_se, INFINITE);
        if (!vmav_status_ok(se_ctx.status) && vmav_status_ok(st)) {
            st = se_ctx.status;
        }
    }

    DWORD exit_code = 1;
    if (proc_inited) {
        if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
            exit_code = 1;
        }
    }
    out->exit_code = (int)exit_code;
    out->wall_ms = vmav_time_now_ms() - t_start;

cleanup:
    if (thread_so != NULL) {
        CloseHandle(thread_so);
    }
    if (thread_se != NULL) {
        CloseHandle(thread_se);
    }
    if (proc_inited) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (stdout_rd != NULL) {
        CloseHandle(stdout_rd);
    }
    if (stdout_wr != NULL) {
        CloseHandle(stdout_wr);
    }
    if (stderr_rd != NULL) {
        CloseHandle(stderr_rd);
    }
    if (stderr_wr != NULL) {
        CloseHandle(stderr_wr);
    }
    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }

    if (!vmav_status_ok(st)) {
        vmav_subproc_result_free(out);
    }
    return st;
}

#endif /* _WIN32 */
