/**
 * @file main.c
 * @brief Entry point for vmavificient.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media_analysis.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"
#include "media_naming.h"
#include "media_tracks.h"
#include "tmdb.h"
#include "utils.h"

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [options] <input_file>\n"
          "\n"
          "Options:\n"
          "  --tmdb <id>   TMDB movie ID for naming (requires TMDB_API_KEY)\n"
          "  --help        Show this help\n",
          prog);
}

/**
 * @brief Prompt user to choose a French audio variant interactively.
 */
static FrenchVariant ask_french_variant(void) {
  printf("\nFrench audio detected. Select variant:\n"
         "  1) VFF (France)\n"
         "  2) VFQ (Quebec)\n"
         "  3) VFI (International)\n"
         "Choice [1-3]: ");
  fflush(stdout);

  char line[16];
  if (!fgets(line, sizeof(line), stdin))
    return FRENCH_VARIANT_VFF;

  switch (atoi(line)) {
  case 2:
    return FRENCH_VARIANT_VFQ;
  case 3:
    return FRENCH_VARIANT_VFI;
  default:
    return FRENCH_VARIANT_VFF;
  }
}

/**
 * @brief Prompt user to choose a source type interactively.
 */
static SourceType ask_source(void) {
  printf("\nSource not detected from filename. Select source:\n"
         "  1) BluRay\n"
         "  2) WEB-DL\n"
         "  3) WEBRip\n"
         "Choice [1-3]: ");
  fflush(stdout);

  char line[16];
  if (!fgets(line, sizeof(line), stdin))
    return SOURCE_BLURAY;

  switch (atoi(line)) {
  case 2:
    return SOURCE_WEBDL;
  case 3:
    return SOURCE_WEBRIP;
  default:
    return SOURCE_BLURAY;
  }
}

int main(int argc, char *argv[]) {
  init_logging();

  if (check_dependencies() != 0) {
    fprintf(stderr, "Fatal: dependency sanity check failed.\n");
    return 1;
  }

  /* ---- CLI parsing ---- */
  int tmdb_id = 0;
  static struct option long_options[] = {
      {"tmdb", required_argument, 0, 't'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
    switch (opt) {
    case 't':
      tmdb_id = atoi(optarg);
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  const char *filepath = NULL;
  if (optind < argc)
    filepath = argv[optind];
  else
    filepath = DEFAULT_TEST_FILE;

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
    printf("\nHDR10:            %s\n", hdr.has_hdr10 ? "yes" : "no");
    printf("Dolby Vision:     %s", hdr.has_dolby_vision ? "yes" : "no");
    if (hdr.has_dolby_vision)
      printf(" (profile %d, level %d)", hdr.dv_profile, hdr.dv_level);
    printf("\nHDR10+:           %s\n", hdr.has_hdr10plus ? "yes" : "no");
  }

  /* ---- Audio tracks ---- */
  MediaTracks tracks = get_media_tracks(filepath);
  if (tracks.error == 0) {
    printf("\nAudio tracks (%d):\n", tracks.audio_count);
    for (int i = 0; i < tracks.audio_count; i++) {
      printf("  #%d  %-6s  %-8s  %dch  %lld kbps  %s\n",
             tracks.audio[i].index, tracks.audio[i].language,
             tracks.audio[i].codec, tracks.audio[i].channels,
             (long long)(tracks.audio[i].bitrate / 1000),
             tracks.audio[i].name);
    }

    /* ---- Best audio per language ---- */
    int best_count = 0;
    TrackInfo *best = select_best_audio_per_language(&tracks, &best_count);
    if (best) {
      printf("\nBest audio per language (%d):\n", best_count);
      for (int i = 0; i < best_count; i++) {
        printf("  #%d  %-6s  %-8s  %dch  %lld kbps  %s\n", best[i].index,
               best[i].language, best[i].codec, best[i].channels,
               (long long)(best[i].bitrate / 1000), best[i].name);
      }
      free(best);
    }

    printf("\nSubtitle tracks (%d):\n", tracks.subtitle_count);
    for (int i = 0; i < tracks.subtitle_count; i++) {
      const char *type = "full";
      if (tracks.subtitles[i].is_forced)
        type = "forced";
      else if (tracks.subtitles[i].is_sdh)
        type = "sdh";
      printf("  #%d  %-6s  %-18s  [%-6s]  %s\n", tracks.subtitles[i].index,
             tracks.subtitles[i].language[0] ? tracks.subtitles[i].language
                                             : "und",
             tracks.subtitles[i].codec, type, tracks.subtitles[i].name);
    }
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

  /* ---- TMDB naming ---- */
  if (tmdb_id > 0) {
    printf("\nFetching TMDB info for ID %d...\n", tmdb_id);
    TmdbMovieInfo tmdb = tmdb_fetch_movie(tmdb_id);
    if (tmdb.error != 0) {
      fprintf(stderr, "Failed to fetch TMDB info.\n");
    } else {
      printf("  Title:    %s\n", tmdb.original_title);
      printf("  Year:     %d\n", tmdb.release_year);
      printf("  Language: %s\n", tmdb.original_language);

      SourceType source = detect_source_from_filename(filepath);
      if (source == SOURCE_UNKNOWN)
        source = ask_source();

      /* Determine French variant if French audio is present. */
      FrenchVariant fv = FRENCH_VARIANT_UNKNOWN;
      bool has_french = false;
      if (tracks.error == 0) {
        for (int i = 0; i < tracks.audio_count; i++) {
          if (strcmp(tracks.audio[i].language, "fre") == 0 ||
              strcmp(tracks.audio[i].language, "fra") == 0) {
            has_french = true;
            break;
          }
        }
      }

      if (has_french) {
        fv = detect_french_variant_from_filename(filepath);
        if (fv == FRENCH_VARIANT_UNKNOWN)
          fv = ask_french_variant();
      }

      LanguageTag lang_tag =
          determine_language_tag(&tracks, tmdb.original_language, fv);

      char output_name[1024];
      build_output_filename(output_name, sizeof(output_name),
                            tmdb.original_title, tmdb.release_year, lang_tag,
                            &info, &hdr, source);

      printf("\nOutput filename:\n  %s\n", output_name);
    }
  }

  if (tracks.error == 0)
    free_media_tracks(&tracks);

  return 0;
}
