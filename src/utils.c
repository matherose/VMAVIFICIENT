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

void init_logging(void) { av_log_set_level(AV_LOG_FATAL); }

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

  printf("SVT-AV1:          %s\n", svt_av1_get_version());

  return 0;
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
