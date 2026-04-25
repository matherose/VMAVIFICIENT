/**
 * @file main.c
 * @brief Entry point for vmavificient.
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "audio_encode.h"
#include "config.h"
#include "encode_preset.h"
#include "final_mux.h"
#include "media_analysis.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"
#include "media_naming.h"
#include "media_tracks.h"
#include "rpu_extract.h"
#include "subtitle_convert.h"
#include "tmdb.h"
#include "ui.h"
#include "utils.h"
#include "video_encode.h"

static void print_usage(const char *prog) {
  fprintf(
      stderr,
      "Usage: %s [options] <input_file>\n"
      "\n"
      "Options:\n"
      "  --tmdb <id>      TMDB movie ID for naming (requires TMDB_API_KEY)\n"
      "  --blind          Skip TMDB lookup; name output as <input-stem>.mkv\n"
      "                   (no config.ini required)\n"
      "  --bitrate <kbps> Override target video bitrate (e.g. 3500)\n"
      "  --srt <path>     Additional SRT subtitle file (can be repeated)\n"
      "  --dry-run        Run analysis + naming, print the encoding plan,\n"
      "                   then exit. No audio/RPU/video work, no files written.\n"
      "  --quiet          Compact output: hide informational sections, keep\n"
      "                   only stage status lines + the Plan / Done blocks.\n"
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

/* ---- Track ordering helpers ---- */

static int audio_lang_priority(const char *lang) {
  if (!lang || !lang[0])
    return 99;
  if (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0)
    return 0;
  if (strcmp(lang, "eng") == 0)
    return 1;
  return 50;
}

static int cmp_audio_order(const void *a, const void *b) {
  const TrackInfo *ta = a;
  const TrackInfo *tb = b;
  return audio_lang_priority(ta->language) - audio_lang_priority(tb->language);
}

static int sub_sort_key(const char *lang, int is_forced) {
  if (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0)
    return is_forced ? 0 : 10;
  if (strcmp(lang, "eng") == 0)
    return 20;
  return 50;
}

int main(int argc, char *argv[]) {
  init_logging();
  ui_init();
  time_t encode_start_time = time(NULL);
  printf("vmavificient — SVT-AV1-HDR %s\n", get_svt_av1_version());

  /* Handle --help / -h before config_init so users can discover the CLI
   * without having to provision a config.ini first. Likewise, --blind
   * runs the encode pipeline without any TMDB/release-group metadata, so
   * config.ini is not required in that mode either. */
  bool blind = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--blind") == 0)
      blind = true;
  }

  if (!blind)
    config_init();

  if (check_dependencies() != 0) {
    fprintf(stderr, "Fatal: dependency sanity check failed.\n");
    return 1;
  }

  /* ---- CLI parsing ---- */
  int tmdb_id = 0;
  int cli_bitrate = 0; /* 0 = auto-compute from resolution/grain */
  bool dry_run = false;
  bool quiet = false;
  LanguageTag cli_lang_tag = LANG_TAG_NONE;
  SourceType cli_source = SOURCE_UNKNOWN;
  QualityType cli_quality = QUALITY_LIVEACTION;
  const char *extra_srt_paths[16] = {NULL};
  int extra_srt_count = 0;

  enum {
    OPT_TMDB = 't',
    OPT_HELP = 'h',
    OPT_BITRATE = 'b',
    OPT_SRT = 's',
    /* Language flags (start at 256 to avoid ASCII collision). */
    OPT_BLIND = 255,
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
    /* Auxiliary flags appended at the end so they get the next free
       sequential value without colliding with the explicit OPT_MULTI=256
       anchor above. */
    OPT_DRY_RUN,
    OPT_QUIET,
  };

  static struct option long_options[] = {
      {"tmdb", required_argument, 0, OPT_TMDB},
      {"bitrate", required_argument, 0, OPT_BITRATE},
      {"srt", required_argument, 0, OPT_SRT},
      {"help", no_argument, 0, OPT_HELP},
      {"blind", no_argument, 0, OPT_BLIND},
      {"dry-run", no_argument, 0, OPT_DRY_RUN},
      {"quiet", no_argument, 0, OPT_QUIET},
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
  while ((opt = getopt_long(argc, argv, "hb:s:", long_options, NULL)) != -1) {
    switch (opt) {
    case OPT_TMDB:
      tmdb_id = atoi(optarg);
      break;
    case OPT_BITRATE:
      cli_bitrate = atoi(optarg);
      if (cli_bitrate <= 0) {
        fprintf(stderr, "Error: --bitrate must be a positive integer (kbps)\n");
        return 1;
      }
      break;
    case OPT_SRT:
      if (extra_srt_count < 16)
        extra_srt_paths[extra_srt_count++] = optarg;
      else
        fprintf(stderr, "Warning: too many --srt files, ignoring '%s'\n",
                optarg);
      break;
    case OPT_HELP:
      print_usage(argv[0]);
      return 0;
    case OPT_BLIND:
      /* Already detected in the pre-scan above; nothing more to do here. */
      break;
    case OPT_DRY_RUN:
      dry_run = true;
      break;
    case OPT_QUIET:
      quiet = true;
      break;
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

  /* Apply --quiet now that all flags are parsed. Sections that should
     always render (Encoding plan, Done) bracket themselves with
     ui_set_quiet(0) / ui_set_quiet(1). */
  if (quiet)
    ui_set_quiet(1);

  const char *filepath = NULL;
  if (optind < argc)
    filepath = argv[optind];
  else
    filepath = DEFAULT_TEST_FILE;

  /* ---- Source ---- */
  MediaInfo info = get_media_info(filepath);
  if (info.error != 0) {
    fprintf(stderr, "Failed to analyze file (error %d).\n", info.error);
    return 1;
  }
  ui_section("Source");
  ui_kv("File", "%s", filepath);
  ui_kv("Resolution", "%d×%d", info.width, info.height);
  ui_kv("Duration", "%s", ui_fmt_duration(info.duration));
  ui_kv("Framerate", "%.3f fps", info.framerate);

  /* ---- Color (HDR) ---- */
  HdrInfo hdr = get_hdr_info(filepath);
  if (hdr.error == 0) {
    ui_section("Color");
    ui_kv("HDR10", "%s", hdr.has_hdr10 ? "yes" : "no");
    if (hdr.has_dolby_vision)
      ui_kv("Dolby Vision", "yes  (profile %d, level %d)", hdr.dv_profile,
            hdr.dv_level);
    else
      ui_kv("Dolby Vision", "no");
    ui_kv("HDR10+", "%s", hdr.has_hdr10plus ? "yes" : "no");
  }

  /* ---- Crop ---- */
  CropInfo crop = get_crop_info(filepath);
  if (crop.error == 0 &&
      (crop.top || crop.bottom || crop.left || crop.right)) {
    ui_section("Crop");
    ui_kv("Detected", "T/B %d/%d   L/R %d/%d", crop.top, crop.bottom,
          crop.left, crop.right);
  }

  /* ---- Tracks ---- */
  MediaTracks tracks = get_media_tracks(filepath);
  TrackInfo *best = NULL;
  int best_count = 0;
  if (tracks.error == 0) {
    ui_section("Tracks");
    ui_kv("Audio", "%d source track%s", tracks.audio_count,
          tracks.audio_count == 1 ? "" : "s");
    for (int i = 0; i < tracks.audio_count; i++) {
      ui_row("    #%-2d  %-4s  %-6s  %dch  %lld kbps  %s",
             tracks.audio[i].index, tracks.audio[i].language,
             tracks.audio[i].codec, tracks.audio[i].channels,
             (long long)(tracks.audio[i].bitrate / 1000),
             tracks.audio[i].name);
    }

    int split_fre = (cli_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;
    best = select_best_audio_per_language(&tracks, split_fre, &best_count);
    if (best && best_count > 0) {
      ui_kv("Selected", "%d track%s for encode", best_count,
            best_count == 1 ? "" : "s");
      for (int i = 0; i < best_count; i++) {
        ui_row("    #%-2d  %-4s  %-6s  %dch  %s", best[i].index,
               best[i].language, best[i].codec, best[i].channels,
               best[i].name);
      }
    }

    ui_kv("Subtitles", "%d source track%s", tracks.subtitle_count,
          tracks.subtitle_count == 1 ? "" : "s");
    for (int i = 0; i < tracks.subtitle_count; i++) {
      const char *type = "full";
      if (tracks.subtitles[i].is_forced)
        type = "forced";
      else if (tracks.subtitles[i].is_sdh)
        type = "sdh";
      ui_row("    #%-2d  %-4s  %-7s  %-6s  %s", tracks.subtitles[i].index,
             tracks.subtitles[i].language[0] ? tracks.subtitles[i].language
                                             : "und",
             tracks.subtitles[i].codec, type, tracks.subtitles[i].name);
    }
  }

  /* ---- Grain analysis ---- */
  ui_section("Grain analysis");
  ui_row("Sampling windows from source... (this may take a few minutes)");
  GrainScore grain = get_grain_score(filepath);
  int film_grain = 0;
  if (grain.error == 0) {
    film_grain = get_film_grain_from_score(grain.grain_score,
                                           grain.grain_variance, cli_quality);
    ui_kv("Windows", "%d%s", grain.windows_succeeded,
          grain.windows_succeeded > 4 ? "  (refined)" : "");
    ui_kv("Luma score", "%.4f", grain.grain_score);
    ui_kv("Chroma score", "%.4f", grain.chroma_grain_score);
    ui_kv("Variance", "%.4f", grain.grain_variance);
    ui_kv("Synth level", "%d  (0–50)", film_grain);
  } else {
    ui_stage_fail("Grain analysis", "fell back to defaults");
  }

  const EncodePreset *enc_preset = get_encode_preset(cli_quality, info.height);

  /* ---- Naming setup (TMDB or --blind) ---- */
  char output_name[1024] = "";
  char base_name[1024] = "";
  char output_dir[2048] = "";
  char mkv_title[1024] = "";
  const char *video_language = "und";
  SourceType source = cli_source;
  FrenchVariant fv = FRENCH_VARIANT_UNKNOWN;
  FrenchAudioOrigin fr_audio_origin = FRENCH_AUDIO_VFF;
  LanguageTag resolved_lang_tag = cli_lang_tag;
  bool naming_ok = false;

  if (blind) {
    /* Derive the output name straight from the input filepath: no TMDB,
       no release-group suffix — just `<input-stem>.mkv` next to the
       source file. Used by CI and anyone without a config.ini. */
    const char *fname = strrchr(filepath, '/');
    fname = fname ? fname + 1 : filepath;
    snprintf(base_name, sizeof(base_name), "%s", fname);
    char *dot = strrchr(base_name, '.');
    if (dot)
      *dot = '\0';
    snprintf(output_name, sizeof(output_name), "%s.mkv", base_name);
    snprintf(mkv_title, sizeof(mkv_title), "%s", base_name);

    snprintf(output_dir, sizeof(output_dir), "%s", filepath);
    char *slash = strrchr(output_dir, '/');
    if (slash)
      *(slash + 1) = '\0';
    else
      snprintf(output_dir, sizeof(output_dir), "./");

    if (tracks.error == 0 && tracks.audio_count > 0 &&
        tracks.audio[0].language[0])
      video_language = tracks.audio[0].language;

    naming_ok = true;
  } else if (tmdb_id > 0) {
    ui_section("TMDB lookup");
    ui_kv("Movie ID", "%d", tmdb_id);
    TmdbMovieInfo tmdb = tmdb_fetch_movie(tmdb_id);
    if (tmdb.error != 0) {
      ui_stage_fail("TMDB fetch", "could not fetch movie info");
    } else {
      ui_kv("Title", "%s", tmdb.original_title);
      ui_kv("Year", "%d", tmdb.release_year);
      ui_kv("Language", "%s", tmdb.original_language);

      /* Source: CLI flag > filename detection > interactive prompt. */
      if (source == SOURCE_UNKNOWN)
        source = detect_source_from_filename(filepath);
      if (source == SOURCE_UNKNOWN)
        source = ask_source();

      /* Determine French variant for OPUS naming. */
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
      resolved_lang_tag = lang_tag;

      build_output_filename(output_name, sizeof(output_name),
                            tmdb.original_title, tmdb.release_year, lang_tag,
                            &info, &hdr, source);

      /* Strip .mkv to get base name. */
      snprintf(base_name, sizeof(base_name), "%s", output_name);
      char *ext = strrchr(base_name, '.');
      if (ext && strcmp(ext, ".mkv") == 0)
        *ext = '\0';

      /* Output dir: same directory as input file. */
      snprintf(output_dir, sizeof(output_dir), "%s", filepath);
      char *last_slash = strrchr(output_dir, '/');
      if (last_slash)
        *(last_slash + 1) = '\0';
      else
        snprintf(output_dir, sizeof(output_dir), "./");

      /* ---- Resolve FrenchAudioOrigin ----
         The CLI language tag wins over the filename-derived French variant
         so that e.g. --multivfi on a source with no VFI marker still labels
         tracks as VFI. */
      switch (lang_tag) {
      case LANG_TAG_MULTI_VFI:
      case LANG_TAG_DUAL_VFI:
        fv = FRENCH_VARIANT_VFI;
        break;
      case LANG_TAG_MULTI_VFQ:
      case LANG_TAG_DUAL_VFQ:
        fv = FRENCH_VARIANT_VFQ;
        break;
      case LANG_TAG_MULTI_VFF:
      case LANG_TAG_DUAL_VFF:
      case LANG_TAG_VFF:
      case LANG_TAG_TRUEFRENCH:
      case LANG_TAG_FRENCH:
        fv = FRENCH_VARIANT_VFF;
        break;
      default:
        /* Keep filename-detected fv as-is for MULTI / VO / VOST / etc. */
        break;
      }

      if (strcmp(tmdb.original_language, "fr") == 0) {
        fr_audio_origin = FRENCH_AUDIO_VO;
      } else {
        switch (fv) {
        case FRENCH_VARIANT_VFQ:
          fr_audio_origin = FRENCH_AUDIO_VFQ;
          break;
        case FRENCH_VARIANT_VFI:
          fr_audio_origin = FRENCH_AUDIO_VFI;
          break;
        default:
          fr_audio_origin = FRENCH_AUDIO_VFF;
          break;
        }
      }

      snprintf(mkv_title, sizeof(mkv_title), "%s (%d)", tmdb.original_title,
               tmdb.release_year);
      {
        const char *vlang = iso639_1_to_2b(tmdb.original_language);
        if (vlang)
          video_language = vlang;
      }

      naming_ok = true;
    }
  }

  if (naming_ok) {
    /* ---- Encoding plan ---- */
    int bitrate =
        cli_bitrate > 0
            ? cli_bitrate
            : get_target_bitrate(info.height,
                                 grain.error == 0 ? grain.grain_score : 0.0);

    /* Plan + Dry-run notice always render — the user needs them to decide
       whether to let the encode proceed. */
    int saved_quiet = ui_is_quiet();
    ui_set_quiet(0);
    ui_section("Encoding plan");
    ui_kv("Preset", "%s  (%s)", quality_type_to_string(cli_quality),
          info.height >= 2160 ? "4K" : "HD");
    ui_kv("SVT-AV1", "preset %d, tune %d, keyint %d, ac-bias %.1f",
          enc_preset->preset, enc_preset->tune, enc_preset->keyint,
          enc_preset->ac_bias);
    if (grain.error == 0) {
      int is_4k = info.height >= 2160;
      int is_grainy = grain.grain_score >= 0.08;
      ui_kv("Grain", "level %d  (%s tier)", film_grain,
            is_grainy ? "grainy" : "low-grain");
      ui_kv("Bitrate", "%d kbps VBR  (%s %s tier)", bitrate,
            is_4k ? "4K" : "HD", is_grainy ? "grainy" : "low-grain");
    } else {
      ui_kv("Bitrate", "%d kbps VBR", bitrate);
    }
    ui_kv("Output", "%s%s", output_dir, output_name);

    if (dry_run) {
      ui_section("Dry run");
      ui_row("No files written. Re-run without --dry-run to encode.");
      if (grain.error == 0 && grain.grain_table_path[0] &&
          getenv("VMAV_KEEP_GRAIN_TMP") == NULL)
        remove(grain.grain_table_path);
      if (tracks.error == 0)
        free_media_tracks(&tracks);
      return 0;
    }
    ui_set_quiet(saved_quiet);

    {
      /* ---- OPUS audio encoding ---- */
      int enc_best_count = 0;
      int enc_split_fre = (resolved_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;
      TrackInfo *enc_best = select_best_audio_per_language(
          &tracks, enc_split_fre, &enc_best_count);

      /* Sort: French first, then English, then others */
      if (enc_best && enc_best_count > 1)
        qsort(enc_best, enc_best_count, sizeof(TrackInfo), cmp_audio_order);

      /* Store OPUS paths and track names for final mux */
      char opus_paths[32][4096];
      char audio_names[32][256];
      int opus_count = 0;

      if (enc_best && enc_best_count > 0) {
        ui_section("Audio");
        ui_kv("Encode", "%d track%s → OPUS", enc_best_count,
              enc_best_count == 1 ? "" : "s");
        for (int i = 0; i < enc_best_count && i < 32; i++) {
          /* In VF2 mode, each French track gets its own variant derived from
             the track title ("VFF - DTS-HD…", "VFQ - E-AC3…") so VFF and VFQ
             produce distinct .fre.fr.opus / .fre.ca.opus files. */
          FrenchVariant track_fv = fv;
          FrenchAudioOrigin track_origin = fr_audio_origin;
          if (enc_split_fre &&
              (strcmp(enc_best[i].language, "fre") == 0 ||
               strcmp(enc_best[i].language, "fra") == 0)) {
            FrenchVariant detected =
                (FrenchVariant)detect_track_french_variant(&enc_best[i]);
            if (detected != FRENCH_VARIANT_UNKNOWN) {
              track_fv = detected;
              track_origin = (detected == FRENCH_VARIANT_VFQ) ? FRENCH_AUDIO_VFQ
                             : (detected == FRENCH_VARIANT_VFI)
                                 ? FRENCH_AUDIO_VFI
                                 : FRENCH_AUDIO_VFF;
            }
          }

          char opus_name[2048];
          build_opus_filename(opus_name, sizeof(opus_name), base_name,
                              enc_best[i].language, track_fv);

          snprintf(opus_paths[i], sizeof(opus_paths[i]), "%s%s", output_dir,
                   opus_name);

          /* Build display name for MKV track */
          build_audio_track_name(audio_names[i], sizeof(audio_names[i]),
                                 enc_best[i].language, enc_best[i].channels,
                                 track_origin);

          ui_row("[%d/%d] %s  %s  %dch  %lld kbps  →  \"%s\"", i + 1,
                 enc_best_count, enc_best[i].language, enc_best[i].codec,
                 enc_best[i].channels,
                 (long long)(enc_best[i].bitrate / 1000), audio_names[i]);

          time_t track_t0 = time(NULL);
          OpusEncodeResult r =
              encode_track_to_opus(filepath, &enc_best[i], opus_paths[i]);

          if (r.skipped) {
            ui_stage_skip(opus_name, "already exists");
          } else if (r.error == 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%s",
                     ui_fmt_duration(difftime(time(NULL), track_t0)));
            ui_stage_ok(opus_name, detail);
          } else {
            char err[64];
            snprintf(err, sizeof(err), "error %d", r.error);
            ui_stage_fail(opus_name, err);
          }

          opus_count++;
        }
      }

      /* ---- Subtitle processing ---- */
      char srt_paths[64][4096];
      char srt_names[64][256];
      char srt_langs[64][64];
      int srt_is_forced[64];
      int srt_is_sdh[64];
      int srt_variant[64]; /* FrenchVariant per track (0 = unknown/non-French) */
      int srt_count = 0;
      int sub_split_fre = (resolved_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;

      if (tracks.error == 0 && tracks.subtitle_count > 0) {
        ui_section("Subtitles");
        ui_kv("Process", "%d source track%s", tracks.subtitle_count,
              tracks.subtitle_count == 1 ? "" : "s");

        for (int i = 0; i < tracks.subtitle_count && srt_count < 48; i++) {
          TrackInfo *sub = &tracks.subtitles[i];
          const char *lang = sub->language[0] ? sub->language : "und";

          /* Per-track French variant so VF2 sources keep VFF and VFQ
             subtitles as separate .fre.fr.srt / .fre.ca.srt files. */
          FrenchVariant track_fv = fv;
          FrenchAudioOrigin track_origin = fr_audio_origin;
          int track_variant_key = 0;
          if (sub_split_fre &&
              (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0)) {
            FrenchVariant detected =
                (FrenchVariant)detect_track_french_variant(sub);
            if (detected != FRENCH_VARIANT_UNKNOWN) {
              track_fv = detected;
              track_variant_key = (int)detected;
              track_origin = (detected == FRENCH_VARIANT_VFQ) ? FRENCH_AUDIO_VFQ
                             : (detected == FRENCH_VARIANT_VFI)
                                 ? FRENCH_AUDIO_VFI
                                 : FRENCH_AUDIO_VFF;
            }
          }

          if (is_text_subtitle(sub)) {
            /* Text subtitle: extract directly to SRT via FFmpeg CLI */
            char srt_fname[2048];
            build_srt_filename(srt_fname, sizeof(srt_fname), base_name, lang,
                               track_fv, sub->is_forced, sub->is_sdh);

            snprintf(srt_paths[srt_count], sizeof(srt_paths[0]), "%s%s",
                     output_dir, srt_fname);

            /* Build display name */
            build_subtitle_track_name(
                srt_names[srt_count], sizeof(srt_names[0]), lang, 1,
                sub->is_forced, sub->is_sdh, track_origin);
            snprintf(srt_langs[srt_count], sizeof(srt_langs[0]), "%s", lang);
            srt_is_forced[srt_count] = sub->is_forced;
            srt_is_sdh[srt_count] = sub->is_sdh;
            srt_variant[srt_count] = track_variant_key;

            /* Check if SRT already exists */
            struct stat srt_st;
            if (stat(srt_paths[srt_count], &srt_st) == 0 &&
                srt_st.st_size > 0) {
              ui_stage_skip(srt_fname, "already exists");
              srt_count++;
            } else {
              /* Extract text subtitle using ffmpeg command */
              char cmd[8192];
              /* If already SRT (subrip), copy stream; else convert to srt. */
              const char *codec_arg =
                  (strcmp(sub->codec, "subrip") == 0) ? "copy" : "srt";
              snprintf(cmd, sizeof(cmd),
                       "ffmpeg -y -loglevel error -i \"%s\" -map 0:%d "
                       "-c:s %s \"%s\"",
                       filepath, sub->index, codec_arg, srt_paths[srt_count]);

              ui_row("Extract  #%-2d  %s  %s  →  \"%s\"", sub->index, lang,
                     sub->codec, srt_names[srt_count]);

              int rc = system(cmd);
              if (rc == 0) {
                ui_stage_ok(srt_fname, NULL);
                srt_count++;
              } else {
                char err[64];
                snprintf(err, sizeof(err), "ffmpeg rc=%d", rc);
                ui_stage_fail("Subtitle extraction", err);
              }
            }
          } else if (is_pgs_subtitle(sub)) {
            /* PGS bitmap subtitle: OCR with Tesseract */

            /* Check if a text SRT already exists for this (lang, variant,
               forced, sdh) — dedup is per variant so a VF2 source won't
               drop a VFQ PGS when only a VFF SRT is present. */
            bool srt_exists_for_lang = false;
            for (int j = 0; j < srt_count; j++) {
              if (strcmp(srt_langs[j], lang) == 0 &&
                  srt_variant[j] == track_variant_key &&
                  srt_is_forced[j] == sub->is_forced &&
                  srt_is_sdh[j] == sub->is_sdh) {
                srt_exists_for_lang = true;
                break;
              }
            }

            if (srt_exists_for_lang) {
              char skip_label[64];
              snprintf(skip_label, sizeof(skip_label), "PGS #%d %s",
                       sub->index, lang);
              ui_stage_skip(skip_label, "SRT already available");
              continue;
            }

            char srt_fname[2048];
            build_srt_filename(srt_fname, sizeof(srt_fname), base_name, lang,
                               track_fv, sub->is_forced, sub->is_sdh);

            snprintf(srt_paths[srt_count], sizeof(srt_paths[0]), "%s%s",
                     output_dir, srt_fname);

            build_subtitle_track_name(
                srt_names[srt_count], sizeof(srt_names[0]), lang, 1,
                sub->is_forced, sub->is_sdh, track_origin);
            snprintf(srt_langs[srt_count], sizeof(srt_langs[0]), "%s", lang);
            srt_is_forced[srt_count] = sub->is_forced;
            srt_is_sdh[srt_count] = sub->is_sdh;
            srt_variant[srt_count] = track_variant_key;

            ui_row("OCR      PGS #%-2d  %s  →  \"%s\"", sub->index, lang,
                   srt_names[srt_count]);

            SubtitleConvertResult scr =
                convert_pgs_to_srt(filepath, sub, srt_paths[srt_count], NULL);

            if (scr.skipped) {
              ui_stage_skip(srt_fname, "already exists");
              srt_count++;
            } else if (scr.error == 0 && scr.subtitle_count > 0) {
              char detail[64];
              snprintf(detail, sizeof(detail), "%d subtitles",
                       scr.subtitle_count);
              ui_stage_ok(srt_fname, detail);
              srt_count++;
            } else if (scr.error == 0) {
              ui_stage_skip(srt_fname, "no subtitles extracted");
            } else {
              char err[64];
              snprintf(err, sizeof(err), "OCR error %d", scr.error);
              ui_stage_fail(srt_fname, err);
            }
          }
        }
      }

      /* ---- Add user-supplied SRT files ---- */
      for (int i = 0; i < extra_srt_count && srt_count < 64; i++) {
        snprintf(srt_paths[srt_count], sizeof(srt_paths[0]), "%s",
                 extra_srt_paths[i]);

        /* Try to guess language from filename */
        const char *srt_lang = "und";
        if (strstr(extra_srt_paths[i], ".fre.") ||
            strstr(extra_srt_paths[i], ".fra."))
          srt_lang = "fre";
        else if (strstr(extra_srt_paths[i], ".eng."))
          srt_lang = "eng";
        else if (strstr(extra_srt_paths[i], ".ger.") ||
                 strstr(extra_srt_paths[i], ".deu."))
          srt_lang = "ger";
        else if (strstr(extra_srt_paths[i], ".spa."))
          srt_lang = "spa";
        else if (strstr(extra_srt_paths[i], ".ita."))
          srt_lang = "ita";

        int forced = (strstr(extra_srt_paths[i], "forced") != NULL) ? 1 : 0;
        int sdh = (strstr(extra_srt_paths[i], "sdh") != NULL ||
                   strstr(extra_srt_paths[i], "SDH") != NULL)
                      ? 1
                      : 0;

        build_subtitle_track_name(srt_names[srt_count], sizeof(srt_names[0]),
                                  srt_lang, 1, forced, sdh, fr_audio_origin);
        snprintf(srt_langs[srt_count], sizeof(srt_langs[0]), "%s", srt_lang);
        srt_is_forced[srt_count] = forced;
        srt_is_sdh[srt_count] = sdh;
        srt_variant[srt_count] = 0;

        printf("  [SRT]  %s → \"%s\"\n", extra_srt_paths[i],
               srt_names[srt_count]);
        srt_count++;
      }

      /* ---- Sort subtitles: French Forced → French → English → Others ---- */
      if (srt_count > 1) {
        int order_idx[64];
        int order_key[64];
        for (int i = 0; i < srt_count; i++) {
          order_idx[i] = i;
          order_key[i] = sub_sort_key(srt_langs[i], srt_is_forced[i]);
        }
        /* Stable insertion sort */
        for (int i = 1; i < srt_count; i++) {
          int ki = order_key[i], ii = order_idx[i];
          int j = i - 1;
          while (j >= 0 && order_key[j] > ki) {
            order_key[j + 1] = order_key[j];
            order_idx[j + 1] = order_idx[j];
            j--;
          }
          order_key[j + 1] = ki;
          order_idx[j + 1] = ii;
        }
        /* Reorder parallel arrays */
        char tmp_paths[64][4096], tmp_names[64][256], tmp_langs[64][64];
        int tmp_forced[64];
        int tmp_sdh[64];
        memcpy(tmp_paths, srt_paths, sizeof(srt_paths));
        memcpy(tmp_names, srt_names, sizeof(srt_names));
        memcpy(tmp_langs, srt_langs, sizeof(srt_langs));
        memcpy(tmp_forced, srt_is_forced, sizeof(tmp_forced));
        memcpy(tmp_sdh, srt_is_sdh, sizeof(tmp_sdh));
        for (int i = 0; i < srt_count; i++) {
          int s = order_idx[i];
          memcpy(srt_paths[i], tmp_paths[s], sizeof(srt_paths[0]));
          memcpy(srt_names[i], tmp_names[s], sizeof(srt_names[0]));
          memcpy(srt_langs[i], tmp_langs[s], sizeof(srt_langs[0]));
          srt_is_forced[i] = tmp_forced[s];
          srt_is_sdh[i] = tmp_sdh[s];
        }
      }

      /* ---- RPU extraction (Dolby Vision) ---- */
      char rpu_path[4096] = "";
      if (hdr.error == 0 && hdr.has_dolby_vision) {
        char rpu_name[2048];
        build_rpu_filename(rpu_name, sizeof(rpu_name), base_name);

        snprintf(rpu_path, sizeof(rpu_path), "%s%s", output_dir, rpu_name);

        ui_section("Dolby Vision RPU");
        time_t rpu_t0 = time(NULL);
        RpuExtractResult rpu_res = extract_rpu(filepath, rpu_path);

        if (rpu_res.skipped) {
          ui_stage_skip(rpu_name, "already exists");
        } else if (rpu_res.error == 0) {
          char detail[64];
          snprintf(detail, sizeof(detail), "%d RPUs in %s", rpu_res.rpu_count,
                   ui_fmt_duration(difftime(time(NULL), rpu_t0)));
          ui_stage_ok(rpu_name, detail);
        } else {
          char err[64];
          snprintf(err, sizeof(err), "error %d", rpu_res.error);
          ui_stage_fail(rpu_name, err);
          rpu_path[0] = '\0'; /* Don't use RPU on failure */
        }
      }

      /* ---- AV1 video encoding ---- */
      char av1_video_path[4096];
      snprintf(av1_video_path, sizeof(av1_video_path), "%s%s.video.mkv",
               output_dir, base_name);
      time_t video_t0 = time(NULL);
      VideoEncodeResult vr = {0};

      {
        ui_section("Video encoding");

        VideoEncodeConfig vcfg = {
            .input_path = filepath,
            .output_path = av1_video_path,
            .rpu_path = rpu_path[0] ? rpu_path : NULL,
            .preset = enc_preset,
            .grain_table_path =
                (grain.error == 0 && grain.grain_table_path[0])
                    ? grain.grain_table_path
                    : NULL,
            .film_grain = film_grain,
            .grain_score = grain.error == 0 ? grain.grain_score : 0.0,
            .grain_variance = grain.error == 0 ? grain.grain_variance : 0.0,
            .target_bitrate = bitrate,
            .info = &info,
            .crop = (crop.error == 0) ? &crop : NULL,
            .hdr = &hdr,
        };

        vr = encode_video(&vcfg);

        if (vr.skipped) {
          ui_stage_skip("video.mkv", "already exists");
        } else if (vr.error == 0) {
          char detail[128];
          snprintf(detail, sizeof(detail), "%lld frames, %s in %s",
                   (long long)vr.frames_encoded,
                   ui_fmt_bytes(vr.bytes_written),
                   ui_fmt_duration(difftime(time(NULL), video_t0)));
          ui_stage_ok("video.mkv", detail);
        } else {
          char err[64];
          snprintf(err, sizeof(err), "error %d", vr.error);
          ui_stage_fail("video.mkv", err);
        }

        /* Grain table is no longer needed once the encode has consumed it. */
        if (grain.error == 0 && grain.grain_table_path[0] &&
            getenv("VMAV_KEEP_GRAIN_TMP") == NULL)
          remove(grain.grain_table_path);
      }

      /* ---- Final MKV muxing ---- */
      {
        char final_path[4096];
        snprintf(final_path, sizeof(final_path), "%s%s", output_dir,
                 output_name);

        /* Build mux audio descriptors */
        MuxAudioTrack mux_audio[32];
        for (int i = 0; i < opus_count && i < 32; i++) {
          mux_audio[i].path = opus_paths[i];
          mux_audio[i].language = enc_best ? enc_best[i].language : "und";
          mux_audio[i].track_name = audio_names[i];
          mux_audio[i].is_default = (i == 0) ? 1 : 0;
        }

        /* Build mux subtitle descriptors */
        MuxSubtitleTrack mux_subs[64];
        int sub_default_set = 0;
        for (int i = 0; i < srt_count && i < 64; i++) {
          mux_subs[i].path = srt_paths[i];
          mux_subs[i].language = srt_langs[i];
          mux_subs[i].track_name = srt_names[i];
          mux_subs[i].is_forced = srt_is_forced[i];
          mux_subs[i].is_sdh = srt_is_sdh[i];
          /* Only the first French forced subtitle is default */
          if (!sub_default_set && srt_is_forced[i] &&
              (strcmp(srt_langs[i], "fre") == 0 ||
               strcmp(srt_langs[i], "fra") == 0)) {
            mux_subs[i].is_default = 1;
            sub_default_set = 1;
          } else {
            mux_subs[i].is_default = 0;
          }
        }

        FinalMuxConfig mux_cfg = {
            .video_path = av1_video_path,
            .output_path = final_path,
            .audio = mux_audio,
            .audio_count = opus_count,
            .subs = mux_subs,
            .sub_count = srt_count,
            .title = mkv_title,
            .video_title = mkv_title,
            .video_language = video_language,
            .chapters_source_path = filepath,
        };

        ui_section("Final mux");
        ui_kv("Inputs", "1 video + %d audio + %d subtitle track%s",
              opus_count, srt_count, srt_count == 1 ? "" : "s");
        time_t mux_t0 = time(NULL);
        FinalMuxResult mr = final_mux(&mux_cfg);

        if (mr.skipped) {
          ui_stage_skip(output_name, "already exists");
        } else if (mr.error == 0) {
          char detail[64];
          snprintf(detail, sizeof(detail), "%s",
                   ui_fmt_duration(difftime(time(NULL), mux_t0)));
          ui_stage_ok(output_name, detail);
        } else {
          char err[64];
          snprintf(err, sizeof(err), "error %d", mr.error);
          ui_stage_fail(output_name, err);
        }

        /* Clean up intermediate files on success. Leaving sidecar .srt
           files next to the final MKV causes players to auto-load them
           as external subtitles, overriding the embedded defaults. */
        int removed = 0;
        if (mr.error == 0) {
          for (int i = 0; i < opus_count; i++)
            if (remove(opus_paths[i]) == 0)
              removed++;
          for (int i = 0; i < srt_count; i++) {
            /* Skip user-supplied --srt files: they live outside output_dir
               or weren't created by us. Only remove SRTs we wrote into
               the output directory. */
            if (strncmp(srt_paths[i], output_dir, strlen(output_dir)) == 0)
              if (remove(srt_paths[i]) == 0)
                removed++;
          }
          if (remove(av1_video_path) == 0)
            removed++;
          if (rpu_path[0] && remove(rpu_path) == 0)
            removed++;
          if (removed > 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%d intermediate file%s", removed,
                     removed == 1 ? "" : "s");
            ui_stage_ok("Cleanup", detail);
          }
        }

        /* ---- Done receipt ---- */
        if (mr.error == 0 && !mr.skipped) {
          struct stat fst;
          long long final_bytes = 0;
          if (stat(final_path, &fst) == 0)
            final_bytes = (long long)fst.st_size;

          double elapsed = difftime(time(NULL), encode_start_time);
          double avg_kbps = 0.0;
          if (info.duration > 0.5 && final_bytes > 0)
            avg_kbps = ((double)final_bytes * 8.0) /
                       (info.duration * 1000.0);
          double delta_pct =
              bitrate > 0 ? (avg_kbps - bitrate) / bitrate * 100.0 : 0.0;
          double speed = elapsed > 0.5 ? info.duration / elapsed : 0.0;

          /* Done receipt always renders — it's the headline result. */
          int saved_quiet_done = ui_is_quiet();
          ui_set_quiet(0);
          ui_section("Done");
          ui_kv("Output", "%s", final_path);
          ui_kv("Size", "%s", ui_fmt_bytes(final_bytes));
          if (bitrate > 0 && avg_kbps > 0)
            ui_kv("Bitrate", "%.0f kbps avg  (%+.1f%% vs %d kbps target)",
                  avg_kbps, delta_pct, bitrate);
          ui_kv("Duration", "%s  encoded in %s  (%.2f× realtime)",
                ui_fmt_duration(info.duration), ui_fmt_duration(elapsed),
                speed);
          ui_set_quiet(saved_quiet_done);
        }
      }

      if (enc_best)
        free(enc_best);
    }
  }

  if (tracks.error == 0)
    free_media_tracks(&tracks);

  return 0;
}
