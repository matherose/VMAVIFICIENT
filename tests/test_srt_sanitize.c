/**
 * @file test_srt_sanitize.c
 * @brief Unit tests for srt_sanitize (<font> tag stripping in SRT text).
 */

#include <stdio.h>
#include <string.h>

#include "srt_sanitize.h"
#include "test_suites.h"

static int failures = 0;

#define CHECK_STRIP(input, expected)                                                       \
  do {                                                                                     \
    char buf[1024];                                                                        \
    snprintf(buf, sizeof(buf), "%s", (input));                                             \
    size_t len = srt_strip_font_tags(buf);                                                 \
    if (strcmp(buf, (expected)) != 0 || len != strlen(expected)) {                         \
      fprintf(stderr, "FAIL %s:%d:\n  got:  \"%s\" (len %zu)\n  want: \"%s\"\n", __FILE__, \
              __LINE__, buf, len, (expected));                                             \
      failures++;                                                                          \
    }                                                                                      \
  } while (0)

static void test_strip_basic(void) {
  /* Real output of ffmpeg's ASS→SRT conversion (nested tags, sizes in ASS
     script pixels). */
  CHECK_STRIP("<font face=\"Trebuchet MS\"><font size=\"66\"><b><i>L'état d'urgence</i></b>"
              "</font></font>",
              "<b><i>L'état d'urgence</i></b>");
  CHECK_STRIP("<font face=\"Cambria\"><font size=\"65\"><font color=\"#e7e7e7\"><b>"
              "<font size=\"85\">2015 APR. J.-C.<font size=\"65\"></font></font></b>"
              "</font></font></font>",
              "<b>2015 APR. J.-C.</b>");

  /* Attribute-less and case-insensitive forms. */
  CHECK_STRIP("<font>plain</font>", "plain");
  CHECK_STRIP("<FONT SIZE=\"20\">x</FONT>", "x");

  /* Untouched content. */
  CHECK_STRIP("no tags at all", "no tags at all");
  CHECK_STRIP("<i>italic</i> and <b>bold</b>", "<i>italic</i> and <b>bold</b>");
  CHECK_STRIP("", "");
}

static void test_strip_edge_cases(void) {
  /* Multi-line cue text: newlines survive. */
  CHECK_STRIP("<font size=\"66\"><i>line one</i>\n<i>line two</i></font>",
              "<i>line one</i>\n<i>line two</i>");

  /* Not font tags: left alone. */
  CHECK_STRIP("<fontx>keep</fontx>", "<fontx>keep</fontx>");
  CHECK_STRIP("a < b and c > d", "a < b and c > d");

  /* Unterminated opening tag: passed through rather than eating the text. */
  CHECK_STRIP("<font size=\"66\" truncated", "<font size=\"66\" truncated");
}

int test_srt_sanitize_suite(void) {
  test_strip_basic();
  test_strip_edge_cases();
  return failures;
}
