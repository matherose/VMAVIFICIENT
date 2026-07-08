/* If nftw needs it under strict -std=c11 (CMAKE_C_EXTENSIONS is OFF), this
   must come before ALL includes: */
#define _XOPEN_SOURCE 700

#include "proc.h"

#include <errno.h>
#include <ftw.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int vmav_run(char *const argv[]) {
  pid_t pid;
  int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
  if (rc != 0) {
    fprintf(stderr, "  Error: cannot spawn '%s': %s\n", argv[0], strerror(rc));
    return -1;
  }
  int status;
  if (waitpid(pid, &status, 0) < 0)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int rm_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)st;
  (void)flag;
  (void)ftw;
  return remove(path);
}

int vmav_rmtree(const char *path) {
  return nftw(path, rm_cb, 32, FTW_DEPTH | FTW_PHYS);
}

int vmav_rmtree_async(const char *path) {
  /* Double-fork so the grandchild is reparented to init and we never
     leave a zombie nor disturb any future SIGCHLD handling. */
  pid_t pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    pid_t inner = fork();
    if (inner == 0) {
      (void)vmav_rmtree(path);
      _exit(0);
    }
    _exit(inner < 0 ? 1 : 0);
  }
  int status;
  (void)waitpid(pid, &status, 0);
  return 0;
}

void vmav_cmd_init(VmavCommand *c) {
  memset(c, 0, sizeof(*c));
}

void vmav_cmd_arg(VmavCommand *c, const char *arg) {
  size_t len = strlen(arg) + 1;
  if (c->argc >= (int)(sizeof(c->argv) / sizeof(c->argv[0])) - 1 || c->pos + len > sizeof(c->buf)) {
    c->overflow = 1;
    return;
  }
  memcpy(c->buf + c->pos, arg, len);
  c->argv[c->argc++] = c->buf + c->pos;
  c->argv[c->argc] = NULL;
  c->pos += len;
}

void vmav_cmd_argf(VmavCommand *c, const char *fmt, ...) {
  char tmp[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= sizeof(tmp)) {
    c->overflow = 1;
    return;
  }
  vmav_cmd_arg(c, tmp);
}
