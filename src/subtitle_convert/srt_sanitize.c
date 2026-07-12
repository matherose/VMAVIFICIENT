/**
 * @file srt_sanitize.c
 * @brief Strip renderer-hostile styling tags from SRT subtitle text.
 *
 * Pure string/stdio logic — no FFmpeg or Tesseract dependency, so this
 * module is also linked into the vmav_tests binary.
 */

#include "srt_sanitize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** @return Non-zero if @p p starts a <font ...> opening tag or </font>. */
static size_t font_tag_length(const char *p) {
  if (strncasecmp(p, "</font>", 7) == 0)
    return 7;
  /* Opening tag: "<font" followed by '>' or whitespace before attributes.
     "<fontx>" or an unterminated "<font ..." (no '>') is left untouched. */
  if (strncasecmp(p, "<font", 5) == 0 &&
      (p[5] == '>' || p[5] == ' ' || p[5] == '\t' || p[5] == '\r' || p[5] == '\n')) {
    const char *close = strchr(p + 5, '>');
    if (close)
      return (size_t)(close - p) + 1;
  }
  return 0;
}

size_t srt_strip_font_tags(char *text) {
  const char *src = text;
  char *dst = text;

  while (*src) {
    if (*src == '<') {
      size_t tag_len = font_tag_length(src);
      if (tag_len > 0) {
        src += tag_len;
        continue;
      }
    }
    *dst++ = *src++;
  }
  *dst = '\0';
  return (size_t)(dst - text);
}

int srt_strip_font_tags_file(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return -1;

  if (fseek(fp, 0, SEEK_END) != 0) {
    (void)fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
    (void)fclose(fp);
    return -1;
  }

  char *buf = malloc((size_t)size + 1);
  if (!buf) {
    (void)fclose(fp);
    return -1;
  }
  size_t nread = fread(buf, 1, (size_t)size, fp);
  (void)fclose(fp);
  if (nread != (size_t)size) {
    free(buf);
    return -1;
  }
  buf[size] = '\0';

  size_t new_len = srt_strip_font_tags(buf);
  if (new_len == (size_t)size) {
    free(buf); /* nothing stripped — skip the rewrite */
    return 0;
  }

  fp = fopen(path, "wb");
  if (!fp) {
    free(buf);
    return -1;
  }
  size_t nwritten = fwrite(buf, 1, new_len, fp);
  free(buf);
  if (fclose(fp) != 0 || nwritten != new_len)
    return -1;
  return 0;
}
