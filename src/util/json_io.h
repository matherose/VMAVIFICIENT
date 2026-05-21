#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>

/* Thin wrappers over cJSON for the common read/write patterns used by
 * vmav_cache and the analyze/search JSON output modes. cJSON itself
 * is intentionally hidden from public headers — we forward-declare its
 * struct so callers don't transitively need <cJSON.h>. */

struct cJSON;

#ifdef __cplusplus
extern "C" {
#endif

/* Read an entire file, then cJSON_Parse it. Returns OK and writes a
 * fresh cJSON tree (caller frees with vmav_json_free) to `*out_root`.
 * On any error `*out_root` is NULL. */
vmav_status_t vmav_json_read_file(const char *path, struct cJSON **out_root);

/* Serialize `root` to a temp file, fsync, then atomically rename to
 * `path`. Renaming over an existing file is allowed. */
vmav_status_t vmav_json_write_atomic(const char *path, const struct cJSON *root);

/* Free a tree previously returned by vmav_json_read_file. Safe on NULL. */
void vmav_json_free(struct cJSON *root);

/* Field accessors with defaults. Each returns the field value if it
 * exists with the expected type, otherwise the provided default. */
const char *vmav_json_get_string(const struct cJSON *obj, const char *key, const char *defval);
double vmav_json_get_number(const struct cJSON *obj, const char *key, double defval);
bool vmav_json_get_bool(const struct cJSON *obj, const char *key, bool defval);
int vmav_json_get_int(const struct cJSON *obj, const char *key, int defval);

#ifdef __cplusplus
}
#endif
