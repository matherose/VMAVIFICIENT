/**
 * @file ui.h
 * @brief Terminal UI helpers — section headers, key/value rows, status markers,
 *        duration / byte-size formatters.
 *
 * Color is auto-disabled when stdout is not a TTY or when NO_COLOR is set in
 * the environment (per https://no-color.org). Box-drawing characters are
 * Unicode (U+2500), supported by every modern terminal.
 */

#ifndef VMAV_UI_H
#define VMAV_UI_H

#include <stddef.h>

/** Total visual width of section header rules. */
#define UI_WIDTH 70

/** Label column width in ui_kv() output. */
#define UI_LABEL_W 14

/**
 * @brief Initialize TTY / color detection. Idempotent. Call once at startup.
 *
 * Honors NO_COLOR=1 and isatty(stdout). After this call, all ui_* output
 * either includes ANSI color codes (TTY + color enabled) or omits them.
 */
void ui_init(void);

/**
 * @brief Enable / disable quiet mode.
 *
 * In quiet mode, ui_section / ui_kv / ui_row become no-ops so the user
 * sees only stage status lines (ui_stage_*). Call ui_set_quiet(0) to
 * temporarily un-quiet around blocks that should always render — the
 * Encoding plan and Done receipt are the canonical examples.
 */
void ui_set_quiet(int quiet);

/** @brief Return current quiet state (0 = normal, non-zero = quiet). */
int ui_is_quiet(void);

/** Print "─── Title ────...─" filling UI_WIDTH visible columns. */
void ui_section(const char *title);

/** Print "  label<pad>value\n" with label column padded to UI_LABEL_W. */
void ui_kv(const char *label, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** Print "  freeform line\n" — two-space indent, no label column. */
void ui_row(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Print "[✓] name  detail\n" — green check. detail may be NULL. */
void ui_stage_ok(const char *name, const char *detail);

/** Print "[-] name  reason\n" — dim dash. reason may be NULL. */
void ui_stage_skip(const char *name, const char *reason);

/** Print "[✗] name  reason\n" — red cross to stderr. reason may be NULL. */
void ui_stage_fail(const char *name, const char *reason);

/**
 * @brief Format a duration into a rotating thread-unsafe buffer.
 *
 * Returns "Ns" for < 60s, "Mm SSs" for < 1h, "Hh MMm" for >= 1h.
 * Four buffers rotate, so up to four calls in one printf are safe.
 */
const char *ui_fmt_duration(double seconds);

/**
 * @brief Format a byte count into a rotating thread-unsafe buffer.
 *
 * Uses SI units (powers of 1000): B, kB, MB, GB.
 * Four buffers rotate, so up to four calls in one printf are safe.
 */
const char *ui_fmt_bytes(long long bytes);

#endif /* VMAV_UI_H */
