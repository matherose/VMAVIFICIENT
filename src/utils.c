/**
 * @file utils.c
 * @brief Implementation of general-purpose utility helpers.
 */

#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libdovi/rpu_parser.h>
#include <libhdr10plus-rs/hdr10plus.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <EbSvtAv1Enc.h>

void init_logging(void) {
  av_log_set_level(AV_LOG_FATAL);
}

int check_dependencies(void) {
  struct {
    const char *name;
    unsigned int version;
  } fflibs[] = {
      {"libavutil", avutil_version()},
      {"libavcodec", avcodec_version()},
      {"libavformat", avformat_version()},
      {"libavfilter", avfilter_version()},
      {"libswscale", swscale_version()},
      {"libswresample", swresample_version()},
  };

  for (size_t i = 0; i < sizeof(fflibs) / sizeof(fflibs[0]); i++) {
    if (fflibs[i].version == 0) {
      fprintf(stderr, "Error: %s did not report a valid version.\n",
              fflibs[i].name);
      return -1;
    }
  }

  dovi_rpu_free(NULL);
  hdr10plus_rs_data_free(NULL);

  return 0;
}

const char *get_svt_av1_version(void) {
  return svt_av1_get_version();
}

bool str_contains_ci(const char *haystack, const char *needle) {
  size_t hlen = strlen(haystack), nlen = strlen(needle);
  if (nlen > hlen)
    return false;
  for (size_t i = 0; i <= hlen - nlen; i++) {
    bool match = true;
    for (size_t j = 0; j < nlen; j++) {
      if (tolower((unsigned char)haystack[i + j]) !=
          tolower((unsigned char)needle[j])) {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
}

void shell_quote_append(char *dst, size_t cap, size_t *pos, const char *src) {
  if (cap == 0)
    return;
  if (*pos < cap)
    dst[(*pos)++] = '"';
  for (; src && *src; src++) {
    /* Each iteration writes at most 2 bytes (escape + char); reserve one
       more for the closing quote so we never write past cap-1. */
    if (*pos + 3 >= cap)
      break;
    unsigned char c = (unsigned char)*src;
    if (c == '"' || c == '\\' || c == '$' || c == '`')
      dst[(*pos)++] = '\\';
    dst[(*pos)++] = (char)c;
  }
  if (*pos < cap)
    dst[(*pos)++] = '"';
  if (*pos < cap)
    dst[*pos] = '\0';
  else
    dst[cap - 1] = '\0';
}
