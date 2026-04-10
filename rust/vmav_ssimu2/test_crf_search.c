/* Smoke test for crf_search_run against a real source file.
 *
 * Usage: test_crf_search <source.mkv> <workdir>
 */

#include "crf_search.h"
#include "encode_preset.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <source.mkv> <workdir>\n", argv[0]);
    return 2;
  }
  const char *src = argv[1];
  const char *workdir = argv[2];
  mkdir(workdir, 0755);

  MediaInfo info = get_media_info(src);
  if (info.error != 0) {
    fprintf(stderr, "media info failed: %d\n", info.error);
    return 1;
  }
  printf("source: %dx%d  dur=%.1fs  fps=%.3f\n", info.width, info.height,
         info.duration, info.framerate);

  HdrInfo hdr = get_hdr_info(src);

  QualityType qt = QUALITY_LIVEACTION;
  const EncodePreset *preset = get_encode_preset(qt, info.height);

  /* Blade Runner grain: user said value 8 is correct. */
  int film_grain = 8;

  double target = (info.height >= 2160) ? 80.0 : 74.0;

  CrfSearchConfig cfg = {
      .input_path = src,
      .rpu_path = NULL,
      .preset = preset,
      .info = &info,
      .crop = NULL,
      .hdr = &hdr,
      .film_grain = film_grain,
      .target_p10 = target,
      .sample_count = 2,
      .sample_duration = 10,
      .frame_stride = 1,
      .crf_probes = {25, 35, 45, 55},
      .workdir = workdir,
  };

  CrfSearchResult r = crf_search_run(&cfg);
  if (r.error != 0) {
    fprintf(stderr, "crf_search_run failed: %d\n", r.error);
    return 1;
  }
  printf("\n=== RESULT ===\n");
  printf("target p10     : %.2f\n", target);
  printf("probes ok      : %d\n", r.probes_succeeded);
  printf("recommended CRF: %d\n", r.recommended_crf);
  printf("predicted p10  : %.3f\n", r.predicted_p10);
  return 0;
}
