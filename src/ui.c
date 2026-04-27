/**
 * @file ui.c
 * @brief Terminal UI helpers — see ui.h for the API.
 */

#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- ANSI escape sequences -------------------------------------------- */

#define ANSI_RESET  "\x1b[0m"
#define ANSI_BOLD   "\x1b[1m"
#define ANSI_DIM    "\x1b[2m"
#define ANSI_RED    "\x1b[31m"
#define ANSI_GREEN  "\x1b[32m"

/* ---- Color + quiet + verbose state ------------------------------------ */

static int g_color = 0;
static int g_quiet = 0;
static int g_verbose = 0;

void ui_set_quiet(int quiet) { g_quiet = quiet ? 1 : 0; }
int ui_is_quiet(void) { return g_quiet; }

void ui_set_verbose(int verbose) { g_verbose = verbose ? 1 : 0; }
int ui_is_verbose(void) { return g_verbose; }

void ui_init(void) {
  static int initialized = 0;
  if (initialized)
    return;
  initialized = 1;

  /* https://no-color.org — any non-empty value disables color. */
  const char *no_color = getenv("NO_COLOR");
  if (no_color && no_color[0]) {
    g_color = 0;
    return;
  }
  g_color = isatty(STDOUT_FILENO) ? 1 : 0;
}

/** Returns @p code on a colored TTY, empty string otherwise. */
static const char *c(const char *code) { return g_color ? code : ""; }

/* ---- Section header --------------------------------------------------- */

void ui_section(const char *title) {
  if (g_quiet)
    return;

  size_t title_len = strlen(title);
  /* Layout: "─── " (4 cols) + title + " " (1) + N×"─" */
  size_t used = 4 + title_len + 1;
  size_t fill = (used < UI_WIDTH) ? UI_WIDTH - used : 1;

  printf("\n─── %s%s%s ", c(ANSI_BOLD), title, c(ANSI_RESET));
  for (size_t i = 0; i < fill; i++)
    fputs("─", stdout);
  printf("\n\n");
}

/* ---- Key/value rows --------------------------------------------------- */

void ui_kv(const char *label, const char *fmt, ...) {
  if (g_quiet)
    return;

  printf("  %-*s  ", UI_LABEL_W, label);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}

void ui_row(const char *fmt, ...) {
  if (g_quiet)
    return;

  printf("  ");
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}

/* ---- Stage status lines ----------------------------------------------- */

void ui_stage_ok(const char *name, const char *detail) {
  if (detail && detail[0])
    printf("  %s[OK]%s   %s  %s%s%s\n", c(ANSI_GREEN), c(ANSI_RESET), name,
           c(ANSI_DIM), detail, c(ANSI_RESET));
  else
    printf("  %s[OK]%s   %s\n", c(ANSI_GREEN), c(ANSI_RESET), name);
}

void ui_stage_skip(const char *name, const char *reason) {
  if (reason && reason[0])
    printf("  %s[--]%s   %s  %s%s%s\n", c(ANSI_DIM), c(ANSI_RESET), name,
           c(ANSI_DIM), reason, c(ANSI_RESET));
  else
    printf("  %s[--]%s   %s\n", c(ANSI_DIM), c(ANSI_RESET), name);
}

void ui_stage_fail(const char *name, const char *reason) {
  if (reason && reason[0])
    fprintf(stderr, "  %s[FAIL]%s %s  %s%s%s\n", c(ANSI_RED), c(ANSI_RESET),
            name, c(ANSI_DIM), reason, c(ANSI_RESET));
  else
    fprintf(stderr, "  %s[FAIL]%s %s\n", c(ANSI_RED), c(ANSI_RESET), name);
}

void ui_hint(const char *fmt, ...) {
  fprintf(stderr, "  %s[hint]%s ", c(ANSI_DIM), c(ANSI_RESET));
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

/* ---- Progress bar ----------------------------------------------------- */

#define UI_PROGRESS_BAR_W 30
/** Minimum seconds between draws — keeps a per-frame caller from spamming. */
#define UI_PROGRESS_MIN_INTERVAL_S 0.25

static double monotonic_seconds(void) {
  /* time(NULL) granularity is 1s on most platforms; that's enough for
     throttling. We avoid clock_gettime to stay portable + dep-free. */
  return (double)time(NULL);
}

static void render_bar(char *buf, size_t cap, double pct) {
  if (pct < 0.0) pct = 0.0;
  if (pct > 1.0) pct = 1.0;
  int filled = (int)(pct * UI_PROGRESS_BAR_W);
  size_t i;
  for (i = 0; i < UI_PROGRESS_BAR_W && i + 1 < cap; i++) {
    if ((int)i < filled)
      buf[i] = '=';
    else if ((int)i == filled)
      buf[i] = '>';
    else
      buf[i] = ' ';
  }
  buf[i] = '\0';
}

void ui_progress_start(UiProgress *p, long long total) {
  if (!p) return;
  p->total = total;
  p->start_time_s = (long long)time(NULL);
  p->last_draw_s = 0.0; /* force first update to draw */
}

void ui_progress_update(UiProgress *p, long long current,
                        const char *middle) {
  if (!p || p->total <= 0) return;

  double now = monotonic_seconds();
  if (p->last_draw_s != 0.0 &&
      (now - p->last_draw_s) < UI_PROGRESS_MIN_INTERVAL_S)
    return;
  p->last_draw_s = now;

  double pct = (double)current / (double)p->total;
  if (pct > 1.0) pct = 1.0;

  char bar[UI_PROGRESS_BAR_W + 1];
  render_bar(bar, sizeof(bar), pct);

  double elapsed = now - (double)p->start_time_s;
  char eta_str[32] = "";
  if (pct > 0.01 && elapsed > 1.0) {
    double remaining = elapsed * (1.0 - pct) / pct;
    int eta_min = (int)(remaining / 60);
    int eta_sec = (int)remaining % 60;
    snprintf(eta_str, sizeof(eta_str), "ETA %02d:%02d", eta_min, eta_sec);
  }

  fprintf(stderr, "\r  [%s] %3d%%  %s%s%s   ", bar, (int)(pct * 100),
          middle && middle[0] ? middle : "", middle && middle[0] ? "  " : "",
          eta_str);
  fflush(stderr);
}

void ui_progress_done(UiProgress *p, long long final_count,
                      const char *middle) {
  if (!p) return;
  /* Force a final draw at 100% even if total <= 0, so users always get a
     newline + summary regardless of how the bar was configured. */
  char bar[UI_PROGRESS_BAR_W + 1];
  render_bar(bar, sizeof(bar), 1.0);

  double elapsed = monotonic_seconds() - (double)p->start_time_s;
  int min = (int)(elapsed / 60);
  int sec = (int)elapsed % 60;

  (void)final_count; /* the caller's middle text usually carries the count */

  fprintf(stderr, "\r  [%s] 100%%  %s%sDone in %02d:%02d          \n", bar,
          middle && middle[0] ? middle : "", middle && middle[0] ? "  " : "",
          min, sec);
  fflush(stderr);
}

/* ---- Formatting helpers (rotating static buffers) --------------------- */

#define ROTATE_N 4
#define ROTATE_BUFSZ 32

static char g_dur_bufs[ROTATE_N][ROTATE_BUFSZ];
static int g_dur_idx = 0;

const char *ui_fmt_duration(double seconds) {
  char *buf = g_dur_bufs[g_dur_idx];
  g_dur_idx = (g_dur_idx + 1) % ROTATE_N;

  if (seconds < 0.0)
    seconds = 0.0;
  long s = (long)(seconds + 0.5);
  if (s < 60) {
    snprintf(buf, ROTATE_BUFSZ, "%lds", s);
  } else if (s < 3600) {
    snprintf(buf, ROTATE_BUFSZ, "%ldm %02lds", s / 60, s % 60);
  } else {
    long h = s / 3600;
    long m = (s % 3600) / 60;
    snprintf(buf, ROTATE_BUFSZ, "%ldh %02ldm", h, m);
  }
  return buf;
}

static char g_byte_bufs[ROTATE_N][ROTATE_BUFSZ];
static int g_byte_idx = 0;

const char *ui_fmt_bytes(long long bytes) {
  char *buf = g_byte_bufs[g_byte_idx];
  g_byte_idx = (g_byte_idx + 1) % ROTATE_N;

  if (bytes < 0)
    bytes = 0;
  double b = (double)bytes;
  if (b < 1000.0)
    snprintf(buf, ROTATE_BUFSZ, "%lld B", bytes);
  else if (b < 1000000.0)
    snprintf(buf, ROTATE_BUFSZ, "%.1f kB", b / 1000.0);
  else if (b < 1000000000.0)
    snprintf(buf, ROTATE_BUFSZ, "%.1f MB", b / 1000000.0);
  else
    snprintf(buf, ROTATE_BUFSZ, "%.2f GB", b / 1000000000.0);
  return buf;
}
