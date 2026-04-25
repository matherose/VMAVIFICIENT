/**
 * @file utils.h
 * @brief General-purpose utility helpers for vmavificient.
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>

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
 */
bool str_contains_ci(const char *haystack, const char *needle);

/**
 * @brief Return the linked SVT-AV1-HDR version string.
 *
 * Indirection lets main.c avoid pulling in the SVT-AV1 headers just for
 * the banner.
 */
const char *get_svt_av1_version(void);

#endif /* UTILS_H */
