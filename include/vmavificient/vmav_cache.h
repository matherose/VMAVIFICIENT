#pragma once

#include "vmavificient/vmav_result.h"

struct cJSON;

#ifdef __cplusplus
extern "C" {
#endif

/* The cache lives under vmav_path_cache_dir(), organized as
 *   <cache_dir>/<kind>/<key>.json
 *
 * Each file wraps the payload in a small envelope:
 *   {"schema": N, "kind": "...", "key": "...", "ts": "...", "data": <payload>}
 *
 * Schema bumps invalidate prior entries (vmav_cache_get returns
 * VMAV_ERR_NOT_FOUND so callers transparently recompute). Coordinate
 * any schema change with a `vmav_cache_invalidate_old_schemas` call
 * at startup if older entries need to be deleted (not just ignored). */

#define VMAV_CACHE_SCHEMA 1

typedef enum {
    VMAV_CACHE_KIND_GRAIN_SCORE = 0,
    VMAV_CACHE_KIND_CRF_SEARCH,
    VMAV_CACHE_KIND_PROBE,
    VMAV_CACHE_KIND_COUNT_
} vmav_cache_kind_t;

/* Look up an entry. On hit, returns OK and writes the payload's data
 * sub-tree (cJSON-owned, must be freed via vmav_cache_release). On
 * miss or schema mismatch, returns VMAV_ERR_NOT_FOUND and *out_data
 * is NULL. */
vmav_status_t vmav_cache_get(vmav_cache_kind_t kind, const char *key, struct cJSON **out_data);

/* Store an entry. `data` is *copied* into a fresh envelope; the caller
 * retains ownership and can free `data` after this call returns. The
 * write is atomic (sibling .tmp file + rename). */
vmav_status_t vmav_cache_put(vmav_cache_kind_t kind, const char *key, const struct cJSON *data);

/* Release a payload obtained from vmav_cache_get. Safe on NULL. */
void vmav_cache_release(struct cJSON *data);

/* Stable string name for the kind enum (used in directory names). */
const char *vmav_cache_kind_str(vmav_cache_kind_t kind);

#ifdef __cplusplus
}
#endif
