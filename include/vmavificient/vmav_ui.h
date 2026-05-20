#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Progress bar ============================================= */

typedef struct vmav_ui_progress vmav_ui_progress_t;

/* Create a progress bar that writes to `out`. Auto-detects whether
 * `out` refers to a TTY: when it does, updates are rendered in-place
 * with carriage returns; otherwise each update is appended as a fresh
 * line. `total` may be 0 for indeterminate progress. Caller owns the
 * returned pointer; free with vmav_ui_progress_free. */
vmav_ui_progress_t *vmav_ui_progress_new(FILE *out, const char *label, uint64_t total);

/* Update progress. The bar renders only when enough wall-clock time
 * has passed since the last paint (16 ms by default) so a tight loop
 * doesn't flood stderr. */
void vmav_ui_progress_set(vmav_ui_progress_t *p, uint64_t current);

/* Render the final state and emit a newline. `final_msg` is appended
 * after the bar (NULL means no extra text). */
void vmav_ui_progress_finish(vmav_ui_progress_t *p, const char *final_msg);

void vmav_ui_progress_free(vmav_ui_progress_t *p);

/* === Spinner ================================================== */

typedef struct vmav_ui_spinner vmav_ui_spinner_t;

vmav_ui_spinner_t *vmav_ui_spinner_new(FILE *out, const char *label);
void vmav_ui_spinner_tick(vmav_ui_spinner_t *s);
void vmav_ui_spinner_finish(vmav_ui_spinner_t *s, const char *final_msg);
void vmav_ui_spinner_free(vmav_ui_spinner_t *s);

/* === Key/value table ========================================== */

typedef struct vmav_ui_table vmav_ui_table_t;

/* Title is rendered above the table. Allocates an empty table. */
vmav_ui_table_t *vmav_ui_table_new(const char *title);
vmav_status_t vmav_ui_table_add(vmav_ui_table_t *t, const char *key, const char *value);
void vmav_ui_table_render(const vmav_ui_table_t *t, FILE *out);
void vmav_ui_table_free(vmav_ui_table_t *t);

#ifdef __cplusplus
}
#endif
