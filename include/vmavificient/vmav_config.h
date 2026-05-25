#pragma once

#include "vmavificient/vmav_result.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Persistent user configuration, stored as INI at
 *   <config-dir>/config.ini
 * (Linux/macOS: $XDG_CONFIG_HOME/vmavificient/, Windows: %APPDATA%\vmavificient\).
 *
 * Schema:
 *   [tmdb]
 *   api_key = <v3 api key>
 *
 *   [naming]
 *   release_group = <tag appended to output filenames>
 *
 * Everything is optional. Unset fields stay empty strings. The
 * parser is deliberately tiny — line-based, no escaping, no
 * comments-after-value. */

typedef struct {
    char tmdb_api_key[128];
    char release_group[64];
} vmav_config_t;

void vmav_config_init(vmav_config_t *cfg);

/* Compose <config-dir>/config.ini into `buf`. Returns BAD_ARG on
 * overflow. */
vmav_status_t vmav_config_path(char *buf, size_t bufsize);

/* Load the config. Sets *out from the file when present; returns
 * VMAV_OK with an initialized-empty struct if the file is missing
 * (typical first run). Any other I/O or parse failure is an error. */
vmav_status_t vmav_config_load(vmav_config_t *out);

/* Save the config to disk atomically (write to .tmp + rename).
 * Creates the parent directory if needed. */
vmav_status_t vmav_config_save(const vmav_config_t *cfg);

/* Resolve the TMDB API key with the documented precedence:
 *   1. env TMDB_API_KEY  (overrides config for ad-hoc testing)
 *   2. config.ini tmdb.api_key
 * Returns VMAV_ERR_NOT_FOUND if neither source has a non-empty
 * value. Useful for `--tmdb <id>` callers that need the key. */
vmav_status_t vmav_config_resolve_api_key(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
