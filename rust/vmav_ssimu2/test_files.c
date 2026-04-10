/* Integration test for ssimu2_score_files.
 *
 * Usage: test_files <ref.mkv> [distorted.mkv] [frame_stride]
 *
 * If only <ref> is given, scores ref against itself — expects mean ~= 100.
 * Otherwise scores ref vs distorted and prints all stats.
 */

#include "media_ssimu2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <ref.mkv> [distorted.mkv] [frame_stride]\n",
            argv[0]);
    return 2;
  }
  const char *ref = argv[1];
  const char *dis = (argc >= 3) ? argv[2] : argv[1];
  int stride = (argc >= 4) ? atoi(argv[3]) : 1;
  if (stride < 1)
    stride = 1;

  printf("ref    : %s\n", ref);
  printf("dis    : %s\n", dis);
  printf("stride : %d\n", stride);
  fflush(stdout);

  Ssimu2Result r = ssimu2_score_files(ref, dis, stride);
  if (r.error < 0) {
    fprintf(stderr, "FAIL: ssimu2_score_files returned error %d\n", r.error);
    return 1;
  }

  printf("\n--- results ---\n");
  printf("frames : %d\n", r.frames_scored);
  printf("mean   : %.4f\n", r.mean);
  printf("median : %.4f\n", r.median);
  printf("p10    : %.4f\n", r.p10);
  printf("min    : %.4f\n", r.min);
  printf("max    : %.4f\n", r.max);

  /* If scoring a file against itself, sanity-check that every frame got
   * a perfect-or-near-perfect score. */
  if (strcmp(ref, dis) == 0) {
    if (r.mean < 99.0) {
      fprintf(stderr, "FAIL: self-scoring gave mean %.4f (expected ~100)\n",
              r.mean);
      return 1;
    }
    printf("\nself-scoring sanity check: PASS\n");
  }
  return 0;
}
