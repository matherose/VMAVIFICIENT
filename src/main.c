/**
 * @file main.c
 * @brief Entry point for vmavificient.
 */

#include <stdio.h>

#include "media_analysis.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"
#include "media_tracks.h"
#include "utils.h"

int main(int argc, char *argv[]) {
  if (check_dependencies() != 0) {
    fprintf(stderr, "Fatal: dependency sanity check failed.\n");
    return 1;
  }

  const char *filepath = (argc > 1) ? argv[1] : DEFAULT_TEST_FILE;
  printf("File: %s\n\n", filepath);

  /* ---- Core info ---- */
  MediaInfo info = get_media_info(filepath);
  if (info.error != 0) {
    fprintf(stderr, "Failed to analyze file (error %d).\n", info.error);
    return 1;
  }
  printf("Video dimensions: %dx%d\n", info.width, info.height);
  printf("Duration:         %.2f s\n", info.duration);
  printf("Framerate:        %.3f fps\n", info.framerate);

  /* ---- Crop detection ---- */
  CropInfo crop = get_crop_info(filepath);
  if (crop.error == 0) {
    printf("\nCrop (T/B/L/R):   %d/%d/%d/%d\n", crop.top, crop.bottom,
           crop.left, crop.right);
  }

  /* ---- HDR ---- */
  HdrInfo hdr = get_hdr_info(filepath);
  if (hdr.error == 0) {
    printf("\nDolby Vision:     %s", hdr.has_dolby_vision ? "yes" : "no");
    if (hdr.has_dolby_vision)
      printf(" (profile %d, level %d)", hdr.dv_profile, hdr.dv_level);
    printf("\nHDR10+:           %s\n", hdr.has_hdr10plus ? "yes" : "no");
  }

  /* ---- Audio tracks ---- */
  MediaTracks tracks = get_media_tracks(filepath);
  if (tracks.error == 0) {
    printf("\nAudio tracks (%d):\n", tracks.audio_count);
    for (int i = 0; i < tracks.audio_count; i++) {
      printf("  #%d  %-6s  %-8s  %s\n", tracks.audio[i].index,
             tracks.audio[i].language, tracks.audio[i].codec,
             tracks.audio[i].name);
    }

    printf("\nSubtitle tracks (%d):\n", tracks.subtitle_count);
    for (int i = 0; i < tracks.subtitle_count; i++) {
      printf("  #%d  %-6s  %-8s  %s\n", tracks.subtitles[i].index,
             tracks.subtitles[i].language, tracks.subtitles[i].codec,
             tracks.subtitles[i].name);
    }
    free_media_tracks(&tracks);
  }

  /* ---- Grain analysis ---- */
  printf("\nAnalyzing grain (this may take a moment)...\n");
  GrainScore grain = get_grain_score(filepath);
  if (grain.error == 0) {
    printf("  Frames analyzed: %d\n", grain.frames_analyzed);
    printf("  Avg TOUT:        %.4f%%\n", grain.avg_tout);
    printf("  Avg Y-Range:     %.1f\n", grain.avg_yrange);
    printf("  Grain score:     %.4f\n", grain.grain_score);
  }

  return 0;
}
