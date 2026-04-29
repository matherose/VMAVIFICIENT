/**
 * @file config.h
 * @brief Runtime configuration loaded from config.ini.
 *
 * Search order:
 *   1. $HOME/.config/vmavificient/config.ini  (primary; written by --config)
 *   2. ./config.ini                            (legacy dev fallback)
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

/**
 * @brief Run an interactive setup that writes
 * `$HOME/.config/vmavificient/config.ini` from prompts.
 *
 * Asks for the TMDB API key and the release group, then writes them with
 * `0600` permissions (the API key is sensitive). The directory is
 * created if missing. If a config already exists the user is asked
 * before it gets overwritten. Used by the `--config` CLI flag so a fresh
 * `brew install` user can get to a working state without learning the
 * INI format.
 *
 * @return 0 on success or user-cancelled overwrite, 1 on I/O error.
 */
int config_interactive_setup(void);

#endif /* VMAV_CONFIG_H */
