/**
 * @file utils.h
 * @brief General-purpose utility helpers for vmavificient.
 */

#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/** @brief Default test media file used when no argument is provided. */
#define DEFAULT_TEST_FILE "bbb_sunflower_1080p_30fps_normal.mp4"

/**
 * @brief Set FFmpeg log level to errors-only (suppress verbose warnings).
 */
void init_logging(void);

/**
 * @brief Verify that all linked libraries report valid versions.
 *
 * Checks FFmpeg, libdovi and libhdr10plus. On failure an error message
 * is printed to @c stderr identifying which library did not respond.
 *
 * @return 0 on success, -1 if any check fails.
 */
int check_dependencies(void);

/**
 * @brief Case-insensitive substring search (ASCII-safe).
 *
 * @param haystack String to search in.
 * @param needle Substring to search for.
 * @return true if @p needle is found in @p haystack, false otherwise.
 */
static inline bool str_contains_ci(const char *haystack, const char *needle) {
  if (haystack == NULL || needle == NULL)
    return false;

  size_t hlen = strlen(haystack);
  size_t nlen = strlen(needle);
  if (nlen > hlen)
    return false;
  for (size_t i = 0; i <= hlen - nlen; i++) {
    bool match = true;
    for (size_t j = 0; j < nlen; j++) {
      if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
}

/**
 * @brief Return the linked SVT-AV1-HDR version string.
 *
 * Indirection lets main.c avoid pulling in the SVT-AV1 headers just for
 * the banner.
 */
const char *get_svt_av1_version(void);

/**
 * @brief Append @p src to @p dst as a double-quoted, shell-safe token.
 *
 * Wraps the value in `"…"` and backslash-escapes the four characters that
 * remain special inside double quotes (`"`, `\`, `$`, `` ` ``). Used by
 * every code path that builds a command string for @c system / @c popen
 * from a user-supplied path: without this, a filename containing
 * `$(…)` or backticks would execute as a subshell.
 *
 * @param dst Destination buffer.
 * @param cap Capacity of @p dst, including the trailing NUL.
 * @param pos In/out: current write offset; advanced past the appended quoted
 * token. On overflow the buffer is NUL-terminated and `*pos` is left at @p cap.
 * @param src NUL-terminated source string. NULL is treated as empty.
 */
void shell_quote_append(char *dst, size_t cap, size_t *pos, const char *src);

#endif /* UTILS_H */
