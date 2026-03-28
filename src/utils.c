/**
 * @file utils.c
 * @brief Implementation of general-purpose utility helpers.
 */

#include "utils.h"

#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libdovi/rpu_parser.h>
#include <libhdr10plus-rs/hdr10plus.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

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
