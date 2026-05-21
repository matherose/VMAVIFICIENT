#include "vmavificient/vmav_cache.h"

#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_os.h"

#include "cJSON.h"
#include "util/json_io.h"

#include <stdio.h>
#include <string.h>

const char *vmav_cache_kind_str(vmav_cache_kind_t kind) {
    switch (kind) {
    case VMAV_CACHE_KIND_GRAIN_SCORE:
        return "grain_score";
    case VMAV_CACHE_KIND_CRF_SEARCH:
        return "crf_search";
    case VMAV_CACHE_KIND_PROBE:
        return "probe";
    case VMAV_CACHE_KIND_COUNT_:
        return "?";
    }
    return "?";
}

/* Build the on-disk path for one entry into `out` (which must have
 * room for at least VMAV_PATH_MAX bytes). Does NOT create the parent
 * directory. */
static vmav_status_t
entry_path(vmav_cache_kind_t kind, const char *key, char *out, size_t out_size) {
    if (key == NULL || key[0] == '\0') {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache: empty key");
    }
    /* Reject keys with separators or '..' segments to keep entries
     * confined to <cache_dir>/<kind>/<key>.json. The keys we generate
     * internally (sha1 hashes) never contain these. */
    for (const char *p = key; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '\0') {
            return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache: invalid char in key '%s'", key);
        }
    }
    if (strstr(key, "..") != NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache: '..' not allowed in key");
    }

    char base[VMAV_PATH_MAX];
    VMAV_TRY(vmav_path_cache_dir(base, sizeof(base)));

    char with_kind[VMAV_PATH_MAX];
    VMAV_TRY(vmav_path_join(with_kind, sizeof(with_kind), base, vmav_cache_kind_str(kind)));

    char with_file[VMAV_PATH_MAX];
    const int n = snprintf(with_file, sizeof(with_file), "%s.json", key);
    if (n < 0 || (size_t)n >= sizeof(with_file)) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache: key too long");
    }
    return vmav_path_join(out, out_size, with_kind, with_file);
}

vmav_status_t vmav_cache_put(vmav_cache_kind_t kind, const char *key, const struct cJSON *data) {
    if (data == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache_put: null data");
    }
    if (kind >= VMAV_CACHE_KIND_COUNT_) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache_put: bad kind");
    }

    char path[VMAV_PATH_MAX];
    VMAV_TRY(entry_path(kind, key, path, sizeof(path)));

    /* Ensure the parent directory exists. */
    char parent[VMAV_PATH_MAX];
    char base[VMAV_PATH_MAX];
    VMAV_TRY(vmav_path_cache_dir(base, sizeof(base)));
    VMAV_TRY(vmav_path_join(parent, sizeof(parent), base, vmav_cache_kind_str(kind)));
    VMAV_TRY(vmav_fs_mkdir_p(parent));

    /* Build the envelope. We duplicate the user's data so callers
     * retain ownership of their tree. */
    cJSON *env = cJSON_CreateObject();
    if (env == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "cJSON_CreateObject failed");
    }
    cJSON_AddNumberToObject(env, "schema", VMAV_CACHE_SCHEMA);
    cJSON_AddStringToObject(env, "kind", vmav_cache_kind_str(kind));
    cJSON_AddStringToObject(env, "key", key);
    char ts[24];
    if (vmav_status_ok(vmav_time_now_iso8601(ts, sizeof(ts)))) {
        cJSON_AddStringToObject(env, "ts", ts);
    }
    cJSON *data_copy = cJSON_Duplicate((const cJSON *)data, /*recurse=*/1);
    if (data_copy == NULL) {
        cJSON_Delete(env);
        return VMAV_ERR(VMAV_ERR_NO_MEM, "cJSON_Duplicate failed");
    }
    cJSON_AddItemToObject(env, "data", data_copy);

    vmav_status_t st = vmav_json_write_atomic(path, env);
    cJSON_Delete(env);
    if (vmav_status_ok(st)) {
        VMAV_LOG_DEBUG("cache put: %s/%s", vmav_cache_kind_str(kind), key);
    }
    return st;
}

vmav_status_t vmav_cache_get(vmav_cache_kind_t kind, const char *key, struct cJSON **out_data) {
    if (out_data == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache_get: null out_data");
    }
    if (kind >= VMAV_CACHE_KIND_COUNT_) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_cache_get: bad kind");
    }
    *out_data = NULL;

    char path[VMAV_PATH_MAX];
    VMAV_TRY(entry_path(kind, key, path, sizeof(path)));

    if (!vmav_fs_exists(path)) {
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "cache miss");
    }

    struct cJSON *env = NULL;
    vmav_status_t st = vmav_json_read_file(path, &env);
    if (!vmav_status_ok(st)) {
        return st;
    }

    const int schema = vmav_json_get_int(env, "schema", -1);
    if (schema != VMAV_CACHE_SCHEMA) {
        VMAV_LOG_DEBUG("cache schema mismatch on %s (have %d, need %d) — treating as miss",
                       path,
                       schema,
                       VMAV_CACHE_SCHEMA);
        cJSON_Delete(env);
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "schema %d != %d", schema, VMAV_CACHE_SCHEMA);
    }

    cJSON *data_field = cJSON_GetObjectItemCaseSensitive(env, "data");
    if (data_field == NULL) {
        cJSON_Delete(env);
        return VMAV_ERR(VMAV_ERR_PARSE, "cache entry missing 'data' field");
    }
    cJSON *data_copy = cJSON_Duplicate((const cJSON *)data_field, 1);
    cJSON_Delete(env);
    if (data_copy == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "cJSON_Duplicate failed");
    }
    *out_data = data_copy;
    return VMAV_OK_STATUS;
}

void vmav_cache_release(struct cJSON *data) {
    if (data != NULL) {
        cJSON_Delete(data);
    }
}
