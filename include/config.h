/**
 * @file config.h
 * @brief Runtime configuration loaded from config.ini.
 *
 * Search order for config.ini:
 *   1. ./config.ini  (current working directory)
 *   2. $HOME/.config/vmavificient/config.ini
 *
 * INI format: `key = value`, `#` and `;` are comments. Sections are ignored.
 * All listed keys are required: vmavificient aborts at startup if config.ini
 * is missing or any required key is empty.
 */

#ifndef VMAV_CONFIG_H
#define VMAV_CONFIG_H

/** @brief Resolved runtime configuration. */
typedef struct {
  char tmdb_api_key[128]; /**< TMDB v3 API key. */
  char release_group[64]; /**< Release group tag, e.g. "matherose". */
} VmavConfig;

/**
 * @brief Load and validate config.ini. Call once at program startup.
 *
 * On failure, prints a clear error to stderr and exits the process — the
 * caller never sees a return value indicating failure.
 */
void config_init(void);

/**
 * @brief Get the loaded configuration. config_init() must have run first.
 */
const VmavConfig *config_get(void);

#endif /* VMAV_CONFIG_H */
