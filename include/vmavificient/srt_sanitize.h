/**
 * @file srt_sanitize.h
 * @brief Strip renderer-hostile styling tags from SRT subtitle text.
 */

#ifndef SRT_SANITIZE_H
#define SRT_SANITIZE_H

#include <stddef.h>

/**
 * @brief Remove all <font ...> and </font> tags from SRT text, in place.
 *
 * FFmpeg's ASS→SRT conversion carries ASS style overrides into the SRT as
 * <font face/size/color> tags. The size values are in ASS script pixels
 * (e.g. 66 for a 1080p PlayRes), but SRT renderers interpret size= as a
 * point size and draw giant text. Inline <i>/<b>/<u> tags are kept.
 *
 * @param text NUL-terminated SRT payload, rewritten in place.
 * @return New length of the string.
 */
size_t srt_strip_font_tags(char *text);

/**
 * @brief Apply srt_strip_font_tags() to a .srt file, rewriting it in place.
 *
 * @param path Path to the SRT file.
 * @return 0 on success, -1 on I/O or allocation error (file left untouched
 *         unless the final write itself failed).
 */
int srt_strip_font_tags_file(const char *path);

#endif /* SRT_SANITIZE_H */
