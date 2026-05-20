#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum path length we are willing to handle. macOS allows 1024,
 * Linux 4096, Windows up to 32767 with the long-path prefix. We pick
 * a comfortable middle ground; functions that take a (buf, size) pair
 * must always validate against the caller-provided size. */
#define VMAV_PATH_MAX 4096

/* === Environment ============================================== */

/* Return the value of an environment variable or NULL if unset.
 * Lifetime: same as getenv() on POSIX; on Windows the value lives in
 * a thread-local buffer that is overwritten on each call. */
const char *vmav_env_get(const char *name);

/* === Terminal ================================================= */

/* True if `fd` (one of STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO)
 * refers to a terminal. False otherwise (pipes, files, /dev/null). */
bool vmav_term_isatty(int fd);

/* True when the user has indicated that ANSI color escapes should be
 * suppressed: the environment variable NO_COLOR is set to a non-empty
 * string, or TERM is "dumb". See https://no-color.org/. */
bool vmav_term_no_color(void);

/* On Windows, enable ENABLE_VIRTUAL_TERMINAL_PROCESSING on stdout so
 * ANSI escapes render correctly. No-op on POSIX. Returns OK even on
 * non-TTY (the call is best-effort). */
vmav_status_t vmav_term_enable_vt(void);

/* === Filesystem =============================================== */

/* True if `path` resolves to an existing file or directory. */
bool vmav_fs_exists(const char *path);

/* True if `path` resolves to an existing directory. */
bool vmav_fs_is_dir(const char *path);

/* Create `path` and any missing parent directories. Equivalent to
 * `mkdir -p`. Existing directories are not an error. */
vmav_status_t vmav_fs_mkdir_p(const char *path);

/* === Paths ==================================================== */

/* Join two path segments with the platform-appropriate separator,
 * writing the result into `out` (which must point to a buffer of
 * `out_size` bytes, including the NUL). Returns VMAV_ERR_BAD_ARG if
 * `out_size` is too small. Either `a` or `b` may be empty. */
vmav_status_t vmav_path_join(char *out, size_t out_size, const char *a, const char *b);

/* Write the per-user configuration directory for vmavificient into
 * `out`. Locations:
 *   POSIX (Linux):  $XDG_CONFIG_HOME/vmavificient or $HOME/.config/vmavificient
 *   macOS:          $HOME/Library/Application Support/vmavificient
 *   Windows:        %APPDATA%\\vmavificient
 * Does not create the directory. */
vmav_status_t vmav_path_config_dir(char *out, size_t out_size);

/* Per-user cache directory. Locations:
 *   POSIX (Linux):  $XDG_CACHE_HOME/vmavificient or $HOME/.cache/vmavificient
 *   macOS:          $HOME/Library/Caches/vmavificient
 *   Windows:        %LOCALAPPDATA%\\vmavificient\\Cache
 */
vmav_status_t vmav_path_cache_dir(char *out, size_t out_size);

/* === Time ===================================================== */

/* Monotonic clock reading in milliseconds since an unspecified epoch.
 * Suitable for measuring durations, not absolute wall time. */
uint64_t vmav_time_now_ms(void);

/* Write the current UTC wall time as an ISO-8601 string
 * "YYYY-MM-DDTHH:MM:SSZ" (20 bytes incl. NUL) into `out`. */
vmav_status_t vmav_time_now_iso8601(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
