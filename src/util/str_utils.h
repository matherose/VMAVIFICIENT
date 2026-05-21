#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Case-insensitive substring search (ASCII-only). */
bool vmav_str_contains_ci(const char *haystack, const char *needle);

/* True if `s` begins with `prefix`. NULL inputs return false. */
bool vmav_str_starts_with(const char *s, const char *prefix);

/* True if `s` ends with `suffix`. */
bool vmav_str_ends_with(const char *s, const char *suffix);

/* Duplicate `s` into a freshly malloc'd buffer. Returns NULL on
 * allocation failure or NULL input. */
char *vmav_str_dup(const char *s);

/* Lowercase ASCII copy: writes a lowercased copy of `src` into `dst`
 * (up to `dst_size` bytes incl. NUL). Non-ASCII bytes pass through. */
void vmav_str_to_lower(const char *src, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif
