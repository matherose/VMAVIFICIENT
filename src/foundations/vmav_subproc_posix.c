/* Anchor declaration so this TU is non-empty on Windows builds. */
typedef int vmav_subproc_posix_anchor_t;

#if defined(_WIN32)
/* Windows impl in vmav_subproc_win.c (Phase 2). */
#else

#include "vmavificient/vmav_subproc.h"

#include "vmavificient/vmav_os.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define VMAV_SUBPROC_BUF_INITIAL 4096
#define VMAV_SUBPROC_BUF_MAX     (16U * 1024U * 1024U) /* 16 MiB sanity cap */

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
        b->cap  = new_cap;
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
    out->stdout_buf.data = NULL;
    out->stdout_buf.len  = 0;
    out->stdout_buf.cap  = 0;
    out->stderr_buf.data = NULL;
    out->stderr_buf.len  = 0;
    out->stderr_buf.cap  = 0;
}

static void close_if_open(int *fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

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

    memset(out, 0, sizeof(*out));

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int devnull        = -1;
    posix_spawn_file_actions_t actions;
    bool actions_inited = false;
    pid_t pid           = -1;
    vmav_status_t st    = VMAV_OK_STATUS;

    if (spec->capture_stdout && pipe(stdout_pipe) != 0) {
        st = VMAV_ERR(VMAV_ERR_SUBPROC, "pipe(stdout): %s", strerror(errno));
        goto cleanup;
    }
    if (spec->capture_stderr && pipe(stderr_pipe) != 0) {
        st = VMAV_ERR(VMAV_ERR_SUBPROC, "pipe(stderr): %s", strerror(errno));
        goto cleanup;
    }
    devnull = open("/dev/null", O_RDONLY);
    if (devnull < 0) {
        st = VMAV_ERR(VMAV_ERR_IO, "open(/dev/null): %s", strerror(errno));
        goto cleanup;
    }

    if (posix_spawn_file_actions_init(&actions) != 0) {
        st = VMAV_ERR(VMAV_ERR_SUBPROC, "posix_spawn_file_actions_init failed");
        goto cleanup;
    }
    actions_inited = true;

    posix_spawn_file_actions_adddup2(&actions, devnull, STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, devnull);
    if (spec->capture_stdout) {
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    }
    if (spec->capture_stderr) {
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
    }

    const uint64_t t_start = vmav_time_now_ms();
    char *const *envp =
        (char *const *)(spec->envp != NULL ? spec->envp : (const char *const *)environ);
    const int spawn_rc =
        posix_spawnp(&pid, spec->exe, &actions, NULL, (char *const *)spec->argv, envp);
    if (spawn_rc != 0) {
        st = VMAV_ERR(VMAV_ERR_SUBPROC, "posix_spawnp('%s'): %s",
                      spec->exe, strerror(spawn_rc));
        pid = -1;
        goto cleanup;
    }

    /* Parent closes the child's pipe ends and the /dev/null. */
    close_if_open(&devnull);
    close_if_open(&stdout_pipe[1]);
    close_if_open(&stderr_pipe[1]);

    /* Phase A — drain captured pipes. Skipped if nothing is captured.
     * Uses poll with a short-ish timeout so timeout enforcement can
     * happen in Phase B even while pipes are still open. */
    struct pollfd pfds[2];
    int n_pfds     = 0;
    int stdout_idx = -1;
    int stderr_idx = -1;
    if (spec->capture_stdout) {
        pfds[n_pfds].fd     = stdout_pipe[0];
        pfds[n_pfds].events = POLLIN;
        stdout_idx          = n_pfds++;
    }
    if (spec->capture_stderr) {
        pfds[n_pfds].fd     = stderr_pipe[0];
        pfds[n_pfds].events = POLLIN;
        stderr_idx          = n_pfds++;
    }
    bool stdout_done = (stdout_idx < 0);
    bool stderr_done = (stderr_idx < 0);
    bool kill_sent   = false;

    while (n_pfds > 0 && (!stdout_done || !stderr_done)) {
        const int n = poll(pfds, (nfds_t)n_pfds, 50);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            st = VMAV_ERR(VMAV_ERR_SUBPROC, "poll: %s", strerror(errno));
            kill(pid, SIGKILL);
            break;
        }
        if (spec->timeout_ms > 0) {
            const uint64_t elapsed = vmav_time_now_ms() - t_start;
            if (elapsed >= spec->timeout_ms) {
                if (!kill_sent) {
                    kill(pid, SIGTERM);
                    kill_sent      = true;
                    out->timed_out = true;
                } else if (elapsed > (uint64_t)spec->timeout_ms + 500) {
                    kill(pid, SIGKILL);
                }
            }
        }
        if (n == 0) {
            continue;
        }
        for (int i = 0; i < n_pfds; i++) {
            if (pfds[i].fd < 0) {
                continue;
            }
            if ((pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            char tmp[8192];
            const ssize_t r = read(pfds[i].fd, tmp, sizeof(tmp));
            if (r > 0) {
                vmav_buf_t *target =
                    (i == stdout_idx) ? &out->stdout_buf : &out->stderr_buf;
                vmav_status_t ast = buf_append(target, tmp, (size_t)r);
                if (!vmav_status_ok(ast)) {
                    st = ast;
                    kill(pid, SIGKILL);
                    goto reap;
                }
            } else if (r == 0) {
                if (i == stdout_idx) {
                    stdout_done = true;
                }
                if (i == stderr_idx) {
                    stderr_done = true;
                }
                close(pfds[i].fd);
                pfds[i].fd = -1;
            } else if (errno != EINTR) {
                st = VMAV_ERR(VMAV_ERR_SUBPROC, "read: %s", strerror(errno));
                kill(pid, SIGKILL);
                goto reap;
            }
        }
    }

reap:
    close_if_open(&stdout_pipe[0]);
    close_if_open(&stderr_pipe[0]);

    /* Phase B — timed reap. Runs regardless of whether pipes were
     * captured. Enforces timeout_ms by SIGTERM-then-SIGKILL escalation
     * and polls for child exit via waitpid(WNOHANG) on a 10ms tick. */
    int wstatus = 0;
    while (pid > 0) {
        const pid_t w = waitpid(pid, &wstatus, WNOHANG);
        if (w == pid) {
            if (WIFEXITED(wstatus)) {
                out->exit_code = WEXITSTATUS(wstatus);
            } else if (WIFSIGNALED(wstatus)) {
                out->signaled   = true;
                out->signal_num = WTERMSIG(wstatus);
                out->exit_code  = 128 + out->signal_num;
            }
            pid = -1;
            break;
        }
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (vmav_status_ok(st)) {
                st = VMAV_ERR(VMAV_ERR_SUBPROC, "waitpid: %s", strerror(errno));
            }
            break;
        }
        if (spec->timeout_ms > 0) {
            const uint64_t elapsed = vmav_time_now_ms() - t_start;
            if (elapsed >= spec->timeout_ms) {
                if (!kill_sent) {
                    kill(pid, SIGTERM);
                    kill_sent      = true;
                    out->timed_out = true;
                } else if (elapsed > (uint64_t)spec->timeout_ms + 500) {
                    kill(pid, SIGKILL);
                }
            }
        }
        const struct timespec sleep_tick = {.tv_sec = 0, .tv_nsec = 10L * 1000L * 1000L};
        nanosleep(&sleep_tick, NULL);
    }
    out->wall_ms = vmav_time_now_ms() - t_start;

cleanup:
    if (actions_inited) {
        posix_spawn_file_actions_destroy(&actions);
    }
    close_if_open(&devnull);
    close_if_open(&stdout_pipe[0]);
    close_if_open(&stdout_pipe[1]);
    close_if_open(&stderr_pipe[0]);
    close_if_open(&stderr_pipe[1]);

    if (!vmav_status_ok(st)) {
        vmav_subproc_result_free(out);
    }
    return st;
}

#endif /* !_WIN32 */
