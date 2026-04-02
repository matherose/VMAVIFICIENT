/**
 * @file main.c
 * @brief Entry point for vmavificient.
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_encode.h"
#include "encode_preset.h"
#include "media_analysis.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"
#include "media_naming.h"
#include "media_tracks.h"
#include "rpu_extract.h"
#include "tmdb.h"
#include "utils.h"
#include "video_encode.h"

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [options] <input_file>\n"
          "\n"
          "Options:\n"
          "  --tmdb <id>      TMDB movie ID for naming (requires TMDB_API_KEY)\n"
          "  --help           Show this help\n"
          "\n"
          "Language flags (override auto-detection):\n"
          "  --multi          MULTi\n"
          "  --multivfi       MULTi.VFI\n"
          "  --multivff       MULTi.VFF\n"
          "  --multivfq       MULTi.VFQ\n"
          "  --multivf2       MULTi.VF2\n"
          "  --multivof       MULTi.VOF\n"
          "  --dual_vfi       DUAL.VFI\n"
          "  --dual_vff       DUAL.VFF\n"
          "  --dual_vfq       DUAL.VFQ\n"
          "  --french         FRENCH\n"
          "  --vff            VFF\n"
          "  --vof            VOF\n"
          "  --truefrench     TRUEFRENCH\n"
          "  --vo             VO\n"
          "  --vost           VOST\n"
          "  --fansub         FANSUB\n"
          "\n"
          "Source flags (override auto-detection):\n"
          "  --bdrip          BDRip\n"
          "  --bluray         BluRay\n"
          "  --remux          REMUX\n"
          "  --dvdrip         DVDRip\n"
          "  --dvdremux       DVDRemux\n"
          "  --webrip         WEBRip\n"
          "  --webdl          WEB-DL\n"
          "  --web            WEB\n"
          "  --hdtv           HDTV\n"
          "  --hdrip          HDRip\n"
          "  --tvrip          TVRip\n"
          "  --vhsrip         VHSRip\n"
          "\n"
          "Quality presets (default: live-action):\n"
          "  --animation       Animation content\n"
          "  --super35_analog  Super 35mm analog film\n"
          "  --super35_digital Super 35mm digital\n"
          "  --imax_analog     IMAX analog film\n"
          "  --imax_digital    IMAX digital\n",
          prog);
}

/**
 * @brief Prompt user to choose a language tag interactively.
 *
 * Lists available audio languages from the source file, then presents
 * all language tag options.
 */
static LanguageTag ask_language_tag(const MediaTracks *tracks) {
  if (tracks && tracks->error == 0 && tracks->audio_count > 0) {
    printf("\nAudio languages found in source:\n");
    char seen[32][8];
    int seen_count = 0;
    for (int i = 0; i < tracks->audio_count; i++) {
      const char *lang =
          tracks->audio[i].language[0] ? tracks->audio[i].language : "und";
      bool dup = false;
      for (int j = 0; j < seen_count; j++) {
        if (strcmp(seen[j], lang) == 0) {
          dup = true;
          break;
        }
      }
      if (!dup && seen_count < 32) {
        snprintf(seen[seen_count], sizeof(seen[0]), "%s", lang);
        seen_count++;
        printf("  - %s\n", lang);
      }
    }
  }

  printf("\nSelect language tag:\n"
         "   1) MULTi          2) MULTi.VFI       3) MULTi.VFF\n"
         "   4) MULTi.VFQ      5) MULTi.VF2       6) MULTi.VOF\n"
         "   7) DUAL.VFI       8) DUAL.VFF        9) DUAL.VFQ\n"
         "  10) FRENCH        11) VFF            12) VOF\n"
         "  13) TRUEFRENCH    14) VO             15) VOST\n"
         "  16) FANSUB\n"
         "Choice [1-16]: ");
  fflush(stdout);

  char line[16];
  if (!fgets(line, sizeof(line), stdin))
    return LANG_TAG_MULTI;

  switch (atoi(line)) {
  case 1:
    return LANG_TAG_MULTI;
  case 2:
    return LANG_TAG_MULTI_VFI;
  case 3:
    return LANG_TAG_MULTI_VFF;
  case 4:
    return LANG_TAG_MULTI_VFQ;
  case 5:
    return LANG_TAG_MULTI_VF2;
  case 6:
    return LANG_TAG_MULTI_VOF;
  case 7:
    return LANG_TAG_DUAL_VFI;
  case 8:
    return LANG_TAG_DUAL_VFF;
  case 9:
    return LANG_TAG_DUAL_VFQ;
  case 10:
    return LANG_TAG_FRENCH;
  case 11:
    return LANG_TAG_VFF;
  case 12:
    return LANG_TAG_VOF;
  case 13:
    return LANG_TAG_TRUEFRENCH;
  case 14:
    return LANG_TAG_VO;
  case 15:
    return LANG_TAG_VOST;
  case 16:
    return LANG_TAG_FANSUB;
  default:
    return LANG_TAG_MULTI;
  }
}

/**
 * @brief Prompt user to choose a source type interactively.
 */
static SourceType ask_source(void) {
  printf("\nSource not detected from filename. Select source:\n"
         "   1) BDRip          2) BluRay          3) REMUX\n"
         "   4) DVDRip         5) DVDRemux        6) WEBRip\n"
         "   7) WEB-DL        8) WEB             9) HDTV\n"
         "  10) HDRip         11) TVRip          12) VHSRip\n"
         "Choice [1-12]: ");
  fflush(stdout);

  char line[16];
  if (!fgets(line, sizeof(line), stdin))
    return SOURCE_BLURAY;

  switch (atoi(line)) {
  case 1:
    return SOURCE_BDRIP;
  case 2:
    return SOURCE_BLURAY;
  case 3:
    return SOURCE_REMUX;
  case 4:
    return SOURCE_DVDRIP;
  case 5:
    return SOURCE_DVDREMUX;
  case 6:
    return SOURCE_WEBRIP;
  case 7:
    return SOURCE_WEBDL;
  case 8:
    return SOURCE_WEB;
  case 9:
    return SOURCE_HDTV;
  case 10:
    return SOURCE_HDRIP;
  case 11:
    return SOURCE_TVRIP;
  case 12:
    return SOURCE_VHSRIP;
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
  LanguageTag cli_lang_tag = LANG_TAG_NONE;
  SourceType cli_source = SOURCE_UNKNOWN;
  QualityType cli_quality = QUALITY_LIVEACTION;

  enum {
    OPT_TMDB = 't',
    OPT_HELP = 'h',
    /* Language flags (start at 256 to avoid ASCII collision). */
    OPT_MULTI = 256,
    OPT_MULTIVFI,
    OPT_MULTIVFF,
    OPT_MULTIVFQ,
    OPT_MULTIVF2,
    OPT_MULTIVOF,
    OPT_DUAL_VFI,
    OPT_DUAL_VFF,
    OPT_DUAL_VFQ,
    OPT_FRENCH,
    OPT_VFF,
    OPT_VOF,
    OPT_TRUEFRENCH,
    OPT_VO,
    OPT_VOST,
    OPT_FANSUB,
    /* Source flags. */
    OPT_BDRIP,
    OPT_BLURAY,
    OPT_REMUX,
    OPT_DVDRIP,
    OPT_DVDREMUX,
    OPT_WEBRIP,
    OPT_WEBDL,
    OPT_WEB,
    OPT_HDTV,
    OPT_HDRIP,
    OPT_TVRIP,
    OPT_VHSRIP,
    /* Quality preset flags. */
    OPT_ANIMATION,
    OPT_SUPER35_ANALOG,
    OPT_SUPER35_DIGITAL,
    OPT_IMAX_ANALOG,
    OPT_IMAX_DIGITAL,
  };

  static struct option long_options[] = {
      {"tmdb", required_argument, 0, OPT_TMDB},
      {"help", no_argument, 0, OPT_HELP},
      /* Language flags. */
      {"multi", no_argument, 0, OPT_MULTI},
      {"multivfi", no_argument, 0, OPT_MULTIVFI},
      {"multivff", no_argument, 0, OPT_MULTIVFF},
      {"multivfq", no_argument, 0, OPT_MULTIVFQ},
      {"multivf2", no_argument, 0, OPT_MULTIVF2},
      {"multivof", no_argument, 0, OPT_MULTIVOF},
      {"dual_vfi", no_argument, 0, OPT_DUAL_VFI},
      {"dual_vff", no_argument, 0, OPT_DUAL_VFF},
      {"dual_vfq", no_argument, 0, OPT_DUAL_VFQ},
      {"french", no_argument, 0, OPT_FRENCH},
      {"vff", no_argument, 0, OPT_VFF},
      {"vof", no_argument, 0, OPT_VOF},
      {"truefrench", no_argument, 0, OPT_TRUEFRENCH},
      {"vo", no_argument, 0, OPT_VO},
      {"vost", no_argument, 0, OPT_VOST},
      {"fansub", no_argument, 0, OPT_FANSUB},
      /* Source flags. */
      {"bdrip", no_argument, 0, OPT_BDRIP},
      {"bluray", no_argument, 0, OPT_BLURAY},
      {"remux", no_argument, 0, OPT_REMUX},
      {"dvdrip", no_argument, 0, OPT_DVDRIP},
      {"dvdremux", no_argument, 0, OPT_DVDREMUX},
      {"webrip", no_argument, 0, OPT_WEBRIP},
      {"webdl", no_argument, 0, OPT_WEBDL},
      {"web", no_argument, 0, OPT_WEB},
      {"hdtv", no_argument, 0, OPT_HDTV},
      {"hdrip", no_argument, 0, OPT_HDRIP},
      {"tvrip", no_argument, 0, OPT_TVRIP},
      {"vhsrip", no_argument, 0, OPT_VHSRIP},
      /* Quality preset flags. */
      {"animation", no_argument, 0, OPT_ANIMATION},
      {"super35_analog", no_argument, 0, OPT_SUPER35_ANALOG},
      {"super35_digital", no_argument, 0, OPT_SUPER35_DIGITAL},
      {"imax_analog", no_argument, 0, OPT_IMAX_ANALOG},
      {"imax_digital", no_argument, 0, OPT_IMAX_DIGITAL},
      {0, 0, 0, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
    switch (opt) {
    case OPT_TMDB:
      tmdb_id = atoi(optarg);
      break;
    case OPT_HELP:
      print_usage(argv[0]);
      return 0;
    /* Language flags. */
    case OPT_MULTI:
      cli_lang_tag = LANG_TAG_MULTI;
      break;
    case OPT_MULTIVFI:
      cli_lang_tag = LANG_TAG_MULTI_VFI;
      break;
    case OPT_MULTIVFF:
      cli_lang_tag = LANG_TAG_MULTI_VFF;
      break;
    case OPT_MULTIVFQ:
      cli_lang_tag = LANG_TAG_MULTI_VFQ;
      break;
    case OPT_MULTIVF2:
      cli_lang_tag = LANG_TAG_MULTI_VF2;
      break;
    case OPT_MULTIVOF:
      cli_lang_tag = LANG_TAG_MULTI_VOF;
      break;
    case OPT_DUAL_VFI:
      cli_lang_tag = LANG_TAG_DUAL_VFI;
      break;
    case OPT_DUAL_VFF:
      cli_lang_tag = LANG_TAG_DUAL_VFF;
      break;
    case OPT_DUAL_VFQ:
      cli_lang_tag = LANG_TAG_DUAL_VFQ;
      break;
    case OPT_FRENCH:
      cli_lang_tag = LANG_TAG_FRENCH;
      break;
    case OPT_VFF:
      cli_lang_tag = LANG_TAG_VFF;
      break;
    case OPT_VOF:
      cli_lang_tag = LANG_TAG_VOF;
      break;
    case OPT_TRUEFRENCH:
      cli_lang_tag = LANG_TAG_TRUEFRENCH;
      break;
    case OPT_VO:
      cli_lang_tag = LANG_TAG_VO;
      break;
    case OPT_VOST:
      cli_lang_tag = LANG_TAG_VOST;
      break;
    case OPT_FANSUB:
      cli_lang_tag = LANG_TAG_FANSUB;
      break;
    /* Source flags. */
    case OPT_BDRIP:
      cli_source = SOURCE_BDRIP;
      break;
    case OPT_BLURAY:
      cli_source = SOURCE_BLURAY;
      break;
    case OPT_REMUX:
      cli_source = SOURCE_REMUX;
      break;
    case OPT_DVDRIP:
      cli_source = SOURCE_DVDRIP;
      break;
    case OPT_DVDREMUX:
      cli_source = SOURCE_DVDREMUX;
      break;
    case OPT_WEBRIP:
      cli_source = SOURCE_WEBRIP;
      break;
    case OPT_WEBDL:
      cli_source = SOURCE_WEBDL;
      break;
    case OPT_WEB:
      cli_source = SOURCE_WEB;
      break;
    case OPT_HDTV:
      cli_source = SOURCE_HDTV;
      break;
    case OPT_HDRIP:
      cli_source = SOURCE_HDRIP;
      break;
    case OPT_TVRIP:
      cli_source = SOURCE_TVRIP;
      break;
    case OPT_VHSRIP:
      cli_source = SOURCE_VHSRIP;
      break;
    /* Quality preset flags. */
    case OPT_ANIMATION:
      cli_quality = QUALITY_ANIMATION;
      break;
    case OPT_SUPER35_ANALOG:
      cli_quality = QUALITY_SUPER35_ANALOG;
      break;
    case OPT_SUPER35_DIGITAL:
      cli_quality = QUALITY_SUPER35_DIGITAL;
      break;
    case OPT_IMAX_ANALOG:
      cli_quality = QUALITY_IMAX_ANALOG;
      break;
    case OPT_IMAX_DIGITAL:
      cli_quality = QUALITY_IMAX_DIGITAL;
      break;
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

  /* ---- Encode preset ---- */
  const EncodePreset *enc_preset = get_encode_preset(cli_quality, info.height);
  printf("\nQuality preset:   %s (%s)\n", quality_type_to_string(cli_quality),
         info.height >= 2160 ? "4K" : "HD");
  printf("  SVT-AV1: preset=%d keyint=%d tune=%d ac-bias=%.1f\n",
         enc_preset->preset, enc_preset->keyint, enc_preset->tune,
         enc_preset->ac_bias);
  printf("  variance-boost=%d variance-octile=%d variance-curve=%d\n",
         enc_preset->variance_boost, enc_preset->variance_octile,
         enc_preset->variance_curve);
  printf("  sharpness=%d luminance-bias=%d tf=%d kf-tf=%d\n",
         enc_preset->sharpness, enc_preset->luminance_bias,
         enc_preset->tf_strength, enc_preset->kf_tf_strength);

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
  int film_grain = 0;
  if (grain.error == 0) {
    printf("  Frames analyzed: %d\n", grain.frames_analyzed);
    printf("  Avg TOUT:        %.4f%%\n", grain.avg_tout);
    printf("  Avg Y-Range:     %.1f\n", grain.avg_yrange);
    printf("  Grain score:     %.4f\n", grain.grain_score);
    film_grain = get_film_grain_from_score(grain.grain_score);
    printf("  Film grain:      %d\n", film_grain);
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

      /* Source: CLI flag > filename detection > interactive prompt. */
      SourceType source = cli_source;
      if (source == SOURCE_UNKNOWN)
        source = detect_source_from_filename(filepath);
      if (source == SOURCE_UNKNOWN)
        source = ask_source();

      /* Determine French variant for OPUS naming. */
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
      if (has_french)
        fv = detect_french_variant_from_filename(filepath);

      /* Language: CLI flag > auto-detection > interactive prompt. */
      LanguageTag lang_tag;
      if (cli_lang_tag != LANG_TAG_NONE) {
        lang_tag = cli_lang_tag;
      } else {
        LanguageTag auto_tag =
            determine_language_tag(&tracks, tmdb.original_language, fv);

        /* If auto-detection produced a definitive result, use it.
           Otherwise ask the user interactively. */
        if (auto_tag != LANG_TAG_VO || tracks.audio_count <= 1) {
          lang_tag = auto_tag;
        } else {
          lang_tag = ask_language_tag(&tracks);
        }
      }

      char output_name[1024];
      build_output_filename(output_name, sizeof(output_name),
                            tmdb.original_title, tmdb.release_year, lang_tag,
                            &info, &hdr, source);

      printf("\nOutput filename:\n  %s\n", output_name);

      /* Strip .mkv to get base name. */
      char base_name[1024];
      snprintf(base_name, sizeof(base_name), "%s", output_name);
      char *ext = strrchr(base_name, '.');
      if (ext && strcmp(ext, ".mkv") == 0)
        *ext = '\0';

      /* Output dir: same directory as input file. */
      char output_dir[2048];
      snprintf(output_dir, sizeof(output_dir), "%s", filepath);
      char *last_slash = strrchr(output_dir, '/');
      if (last_slash)
        *(last_slash + 1) = '\0';
      else
        snprintf(output_dir, sizeof(output_dir), "./");

      /* ---- OPUS audio encoding ---- */
      int enc_best_count = 0;
      TrackInfo *enc_best =
          select_best_audio_per_language(&tracks, &enc_best_count);
      if (enc_best && enc_best_count > 0) {
        printf("\nEncoding %d audio track(s) to OPUS...\n", enc_best_count);
        for (int i = 0; i < enc_best_count; i++) {
          char opus_name[2048];
          build_opus_filename(opus_name, sizeof(opus_name), base_name,
                              enc_best[i].language, fv);

          char opus_path[4096];
          snprintf(opus_path, sizeof(opus_path), "%s%s", output_dir,
                   opus_name);

          printf("  [%d/%d] %s (%s, %dch, %lld kbps)...\n", i + 1,
                 enc_best_count, enc_best[i].language, enc_best[i].codec,
                 enc_best[i].channels,
                 (long long)(enc_best[i].bitrate / 1000));

          OpusEncodeResult r =
              encode_track_to_opus(filepath, &enc_best[i], opus_path);

          if (r.skipped)
            printf("  [SKIP] %s (already exists)\n", opus_name);
          else if (r.error == 0)
            printf("  [OK]   %s\n", opus_name);
          else
            fprintf(stderr, "  [FAIL] %s (error %d)\n", opus_name, r.error);
        }
        free(enc_best);
      }

      /* ---- RPU extraction (Dolby Vision) ---- */
      char rpu_path[4096] = "";
      if (hdr.error == 0 && hdr.has_dolby_vision) {
        char rpu_name[2048];
        build_rpu_filename(rpu_name, sizeof(rpu_name), base_name);

        snprintf(rpu_path, sizeof(rpu_path), "%s%s", output_dir, rpu_name);

        printf("\nExtracting Dolby Vision RPU...\n");
        RpuExtractResult rpu_res = extract_rpu(filepath, rpu_path);

        if (rpu_res.skipped)
          printf("  [SKIP] %s (already exists)\n", rpu_name);
        else if (rpu_res.error == 0)
          printf("  [OK]   %s (%d RPUs)\n", rpu_name, rpu_res.rpu_count);
        else {
          fprintf(stderr, "  [FAIL] %s (error %d)\n", rpu_name, rpu_res.error);
          rpu_path[0] = '\0'; /* Don't use RPU on failure */
        }
      }

      /* ---- AV1 video encoding ---- */
      {
        char av1_path[4096];
        snprintf(av1_path, sizeof(av1_path), "%s%s", output_dir, output_name);

        int bitrate = get_target_bitrate(info.height,
                                          grain.error == 0 ? grain.grain_score : 0.0);
        printf("\nEncoding video to AV1 (%d kbps, %s, %s)...\n",
               bitrate, quality_type_to_string(cli_quality),
               info.height >= 2160 ? "4K" : "HD");

        VideoEncodeConfig vcfg = {
            .input_path = filepath,
            .output_path = av1_path,
            .rpu_path = rpu_path[0] ? rpu_path : NULL,
            .preset = enc_preset,
            .film_grain = film_grain,
            .target_bitrate = bitrate,
            .info = &info,
            .crop = (crop.error == 0) ? &crop : NULL,
            .hdr = &hdr,
        };

        VideoEncodeResult vr = encode_video(&vcfg);

        if (vr.skipped)
          printf("  [SKIP] %s (already exists)\n", output_name);
        else if (vr.error == 0)
          printf("  [OK]   %s (%lld frames, %lld bytes)\n", output_name,
                 (long long)vr.frames_encoded, (long long)vr.bytes_written);
        else
          fprintf(stderr, "  [FAIL] %s (error %d)\n", output_name, vr.error);
      }
    }
  }

  if (tracks.error == 0)
    free_media_tracks(&tracks);

  return 0;
}
