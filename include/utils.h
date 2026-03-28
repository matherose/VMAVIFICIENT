/**
 * @file utils.h
 * @brief General-purpose utility helpers for vmavificient.
 */

#ifndef UTILS_H
#define UTILS_H

/** @brief Default test media file used when no argument is provided. */
#define DEFAULT_TEST_FILE "bbb_sunflower_1080p_30fps_normal.mp4"

/**
 * @brief Verify that all linked libraries report valid versions.
 *
 * Checks FFmpeg, libdovi and libhdr10plus. On failure an error message
 * is printed to @c stderr identifying which library did not respond.
 *
 * @return 0 on success, -1 if any check fails.
 */
int check_dependencies(void);

#endif /* UTILS_H */
