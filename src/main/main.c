/**
 * @file main.c
 * @brief Entry point for vmavificient.
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

/* Cache directory - file scope so helper functions can access it. */
static char g_cache_dir[4096] = "";

/* Defined by the build system (-DVMAV_VERSION=...). Fallback keeps
   non-CMake builds linkable, e.g. ad-hoc clang invocations. */
#ifndef VMAV_VERSION
#define VMAV_VERSION "dev"
#endif

#include "audio_encode.h"
#include "config.h"
#include "crf_search.h"
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

/**
 * @brief Parse a string as a base-10 int.
 *
 * Returns the parsed value, or 0 if the string is NULL, empty, contains
 * non-numeric content (besides leading/trailing whitespace and trailing
 * newlines from fgets), or overflows int. Replaces atoi() — the
 * cert-err34-c rule for the codebase.
 */
static int parse_int_or_zero(const char *s) {
  if (!s)
    return 0;
  char *endptr = NULL;
  errno = 0;
  long v = strtol(s, &endptr, 10);
  if (endptr == s)
    return 0; /* no digits parsed */
  /* Skip trailing whitespace; reject anything else. */
  while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' || *endptr == '\r')
    endptr++;
  if (*endptr != '\0')
    return 0;
  if (errno == ERANGE || v > INT_MAX || v < INT_MIN)
    return 0;
  return (int)v;
}

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [options] <input_file>\n"
          "\n"
          "Options:\n"
          "  --tmdb <id>      TMDB movie ID for naming (requires TMDB_API_KEY)\n"
          "  --blind          Skip TMDB lookup; name output as <input-stem>.mkv\n"
          "                   (no config required)\n"
          "  --config         Run interactive setup once; writes\n"
          "                   $HOME/.config/vmavificient/config.ini with the TMDB\n"
          "                   API key and release group. Subsequent runs read it\n"
          "                   automatically.\n"
          "  --crf <N>        Skip CRF search; encode at this CRF directly\n"
          "                   (1–63, lower = higher quality)\n"
          "  --vmaf-target <N> Override the VMAF target for CRF search\n"
          "                   (default: per-preset, 90–96)\n"
          "  --bitrate <kbps> Skip CRF search; encode VBR at this bitrate\n"
          "  --srt <path>     Additional SRT subtitle file (can be repeated)\n"
          "  --dry-run        Run analysis + CRF search + naming, print the\n"
          "                   encoding plan, then exit. No files written.\n"
          "  --quiet          Compact output: hide informational sections, keep\n"
          "                   only stage status lines + the Plan / Done blocks.\n"
          "  --verbose        Forward SVT-AV1 encoder log messages to stderr\n"
          "                   (rate control, GOP layout, warnings). Composes\n"
          "                   with --quiet.\n"
          "  --grain-only     Like --dry-run, plus dump every encoder knob the\n"
          "                   resolved preset configures (grain mech, tune,\n"
          "                   ac-bias, filters, QMs). For sanity-checking what\n"
          "                   each tier actually does without a full encode.\n"
          "  --companion-hd   After the 4K encode, produce a second 1080p HDLight\n"
          "                   release from the same REMUX source. Requires a 4K\n"
          "                   source. Audio and subtitles are shared between both\n"
          "                   outputs. Dolby Vision is stripped from the HD "
          "output.\n"
          "  --scale-to-hd    Produce only a 1080p HDLight release (no 4K "
          "output).\n"
          "                   Requires a 4K source. Full independent pipeline.\n"
          "                   Mutually exclusive with --companion-hd.\n"
          "  --cache-dir <path>\n"
          "                   Use specified directory for intermediate files\n"
          "                   (grain analysis, CRF search results, extracted\n"
          "                   audio/subtitles). Cache is deleted after successful\n"
          "                   encode. Defaults to a hidden .vmavificient-cache folder\n"
          "                   in the project root.\n"
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
      const char *lang = tracks->audio[i].language[0] ? tracks->audio[i].language : "und";
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

  switch (parse_int_or_zero(line)) {
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

  switch (parse_int_or_zero(line)) {
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

/* ---- Track display helpers ---- */

static const char *codec_short(const char *codec) {
  if (strcmp(codec, "hdmv_pgs_subtitle") == 0)
    return "pgs";
  if (strcmp(codec, "subrip") == 0)
    return "srt";
  if (strcmp(codec, "dvd_subtitle") == 0)
    return "vob";
  if (strcmp(codec, "ass") == 0 || strcmp(codec, "ssa") == 0)
    return "ass";
  return codec;
}

/**
 * @brief Build a path inside the cache directory.
 * Appends the given relative path to g_cache_dir with proper separator.
 */
static void build_cache_path(char *buf, size_t bufsize, const char *relative_path) {
  if (strlen(g_cache_dir) > 0 && g_cache_dir[strlen(g_cache_dir) - 1] == '/')
    snprintf(buf, bufsize, "%s%s", g_cache_dir, relative_path);
  else
    snprintf(buf, bufsize, "%s/%s", g_cache_dir, relative_path);
}

/**
 * @brief Remove the entire cache directory and recreate it.
 * Used after successful encode to cleanup all intermediate files.
 *
 * Atomic behavior: first renames the cache to a temporary name,
 * then creates a new empty cache dir. If rename fails, creates
 * cache dir in place. Old cache is deleted only after successful recreate.
 */
static void cleanup_cache_dir(void) {
  char old_cache_path[4096];
  pid_t pid = getpid();
  snprintf(old_cache_path, sizeof(old_cache_path), "%s.tmp.%d", g_cache_dir, pid);

  /* Try to atomically rename the cache directory */
  if (rename(g_cache_dir, old_cache_path) == 0) {
    /* Rename succeeded: create fresh cache dir */
    if (mkdir(g_cache_dir, 0755) != 0 && errno != EEXIST) {
      fprintf(stderr, "Warning: failed to recreate cache directory '%s' (errno %d)\n", g_cache_dir,
              errno);
      return;
    }
    /* Delete old cache in background (non-blocking) */
    char cmd[4096];
    char escaped_old[4096];
    shell_quote_append(escaped_old, sizeof(escaped_old), &(size_t){0}, old_cache_path);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' &", escaped_old);
    int ret = system(cmd);
    (void)ret; /* Ignore background rm errors */
  } else {
    /* Rename failed (e.g., cache doesn't exist or is in use)
     * Try to recreate in place */
    char cmd[4096];
    char escaped_path[4096];
    size_t j = 0;
    for (size_t i = 0; i < strlen(g_cache_dir) && j < sizeof(escaped_path) - 2; i++) {
      if (g_cache_dir[i] == '\'' || g_cache_dir[i] == '\\')
        escaped_path[j++] = '\\';
      escaped_path[j++] = g_cache_dir[i];
    }
    escaped_path[j] = '\0';
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", escaped_path);
    if (system(cmd) != 0) {
      fprintf(stderr, "Warning: failed to cleanup cache directory '%s'\n", g_cache_dir);
    }
    /* Recreate empty cache directory */
    if (mkdir(g_cache_dir, 0755) != 0 && errno != EEXIST) {
      fprintf(stderr, "Warning: failed to recreate cache directory '%s' (errno %d)\n", g_cache_dir,
              errno);
    }
  }
}

/* ---- Score cache helpers ---- */

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static bool load_cached_scores(const char *cache_path, double *grain_score, double *grain_variance,
                               int *crf) {
  FILE *f = fopen(cache_path, "r");
  if (!f)
    return false;
  char line[4096];
  bool found_grain = false, found_var = false, found_crf = false;
  while (fgets(line, sizeof(line), f)) {
    char value[64];
    if (sscanf(line, " \"grain_score\": %63[^,\"]", value) == 1)
      *grain_score = atof(value), found_grain = true;
    else if (sscanf(line, " \"grain_variance\": %63[^,\"]", value) == 1)
      *grain_variance = atof(value), found_var = true;
    else if (sscanf(line, " \"crf\": %63[^,\"]", value) == 1)
      *crf = atoi(value), found_crf = true;
  }
  fclose(f);
  return found_grain && found_var && found_crf;
}

static bool save_cached_scores(const char *cache_path, double grain_score, double grain_variance,
                               int crf) {
  FILE *f = fopen(cache_path, "w");
  if (!f)
    return false;
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
  fprintf(f,
          "{\n"
          "  \"generated\": \"%s\",\n"
          "  \"grain_score\": %.4f,\n"
          "  \"grain_variance\": %.4f,\n"
          "  \"crf\": %d\n"
          "}\n",
          timestamp, grain_score, grain_variance, crf);
  fclose(f);
  return true;
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

/**
 * @brief Dump every relevant encoder knob from the resolved preset.
 *
 * Called by --grain-only so the user can sanity-check what each tier
 * actually configures without committing to a multi-hour encode.
 * Focuses on the knobs that meaningfully affect grain pass-through and
 * perceived quality — preset / tune / ac-bias, the three filters
 * (TF / CDEF / restoration), variance boost, QMs, the noise vs
 * film-grain mechanism choice. Skips the rate-control plumbing
 * (already in the Plan section) and per-temporal-layer offsets.
 */
static void print_encoder_knobs(const EncodePreset *p, int film_grain) {
  ui_section("Encoder knobs");

  ui_kv("Speed", "preset %d, keyint %d", p->preset, p->keyint);
  ui_kv("Tune", "%d  (0=VQ, 1=PSNR, 5=Film Grain)", p->tune);
  ui_kv("ac-bias", "%.1f", p->ac_bias);
  ui_kv("Sharpness", "%d", p->sharpness);
  ui_kv("Complex HVS", "%s", p->complex_hvs ? "on" : "off");

  /* Grain mechanism — selected per-preset, plus the dynamic level. */
  if (p->use_noise) {
    char size_buf[32];
    if (p->noise_size < 0)
      snprintf(size_buf, sizeof(size_buf), "auto");
    else
      snprintf(size_buf, sizeof(size_buf), "%d", p->noise_size);
    char chroma_buf[64];
    if (p->noise_chroma_strength < 0)
      snprintf(chroma_buf, sizeof(chroma_buf), "auto (~60%% of luma)");
    else if (p->noise_chroma_strength == 0)
      snprintf(chroma_buf, sizeof(chroma_buf), "off");
    else
      snprintf(chroma_buf, sizeof(chroma_buf), "%d", p->noise_chroma_strength);
    ui_kv("Grain mech", "%s", "--noise (synthetic overlay, 4.1.0+)");
    ui_kv("Strength", "%d", film_grain);
    ui_kv("Noise size", "%s", size_buf);
    ui_kv("Noise chroma", "%s", chroma_buf);
    ui_kv("Chroma← luma", "%s", p->noise_chroma_from_luma ? "on" : "off");
  } else {
    ui_kv("Grain mech", "%s", "--film-grain (analyzed) + denoise=1");
    ui_kv("Strength", "%d", film_grain);
  }

  /* Filters that affect grain pass-through. */
  if (p->enable_tf)
    ui_kv("Temporal flt", "on  (frame %d, keyframe %d)", p->tf_strength, p->kf_tf_strength);
  else
    ui_kv("Temporal flt", "off");
  ui_kv("DLF", "%s", p->enable_dlf == 0 ? "off" : p->enable_dlf == 2 ? "accurate" : "on");
  ui_kv("CDEF scaling", "%d", p->cdef_scaling);
  ui_kv("Restoration", "%s",
        p->enable_restoration == 0   ? "off"
        : p->enable_restoration == 1 ? "on"
                                     : "default");
  ui_kv("Noise-adapt", "%d  (0=off, 2=tune-default, 3=CDEF-only, 4=restoration-only)",
        p->noise_adaptive_filtering);

  /* Variance boost (HDR-relevant). */
  ui_kv("Var boost", "strength %d, octile %d, curve %d", p->variance_boost, p->variance_octile,
        p->variance_curve);

  /* Quantization matrices. */
  if (p->enable_qm == 1) {
    char qm_buf[32];
    if (p->qm_min >= 0 && p->qm_max >= 0)
      snprintf(qm_buf, sizeof(qm_buf), "luma %d-%d", p->qm_min, p->qm_max);
    else
      snprintf(qm_buf, sizeof(qm_buf), "default range");
    char chroma_qm_buf[32];
    if (p->chroma_qm_min >= 0 && p->chroma_qm_max >= 0)
      snprintf(chroma_qm_buf, sizeof(chroma_qm_buf), "chroma %d-%d", p->chroma_qm_min,
               p->chroma_qm_max);
    else
      snprintf(chroma_qm_buf, sizeof(chroma_qm_buf), "chroma default");
    ui_kv("Quant matrix", "%s, %s", qm_buf, chroma_qm_buf);
  } else {
    ui_kv("Quant matrix", "off");
  }

  /* Other meaningful knobs. */
  ui_kv("HBD MDS", "%d  (0=default, 1=full 10b, 2=hybrid)", p->hbd_mds);
  ui_kv("Max tx size", "%d", p->max_tx_size);
  ui_kv("QP scale comp", "%.1f", p->qp_scale_compress_strength);

  /* Rate control extras the Plan block doesn't cover. */
  if (p->look_ahead_distance >= 0)
    ui_kv("Look-ahead", "%d frames", p->look_ahead_distance);
  if (p->vbr_max_section_pct > 0)
    ui_kv("VBR max-sec", "%d%%", p->vbr_max_section_pct);
  if (p->gop_constraint_rc == 1)
    ui_kv("GOP RC match", "on");
}

int main(int argc, char *argv[]) {
  init_logging();
  ui_init();
  time_t encode_start_time = time(NULL);
  printf("vmavificient v%s — SVT-AV1-HDR %s\n", VMAV_VERSION, get_svt_av1_version());

  /* Handle --help / -h before config_init so users can discover the CLI
   * without having to provision a config first. Likewise, --blind runs
   * the encode pipeline without any TMDB/release-group metadata, so the
   * config file is not required in that mode either. --config runs the
   * interactive setup and exits; it must also bypass config_init. */
  bool blind = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--config") == 0)
      return config_interactive_setup();
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
  int cli_bitrate = 0;     /* 0 = run crf-search (default path) */
  int cli_crf = 0;         /* 0 = run crf-search; >0 = skip search, use directly */
  int cli_vmaf_target = 0; /* 0 = use per-preset default from get_vmaf_target() */
  bool dry_run = false;
  bool quiet = false;
  bool verbose = false;
  bool grain_only = false;
  bool companion_hd = false;
  bool scale_to_hd = false;
  char cli_cache_dir[4096] = ""; /* CLI-provided cache dir, optional */
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
    OPT_VERBOSE,
    OPT_GRAIN_ONLY,
    /* --config is pre-scanned and dispatched before getopt_long runs;
       it's still registered with getopt so the parser doesn't reject it
       when it shows up alongside other flags. Placed at the end so its
       auto-incremented value can't collide with OPT_MULTI = 256 anchor. */
    OPT_CONFIG_SETUP,
    OPT_CRF,
    OPT_VMAF_TARGET,
    OPT_COMPANION_HD,
    OPT_SCALE_TO_HD,
    OPT_CACHE_DIR,
  };

  static struct option long_options[] = {
      {"tmdb", required_argument, 0, OPT_TMDB},
      {"bitrate", required_argument, 0, OPT_BITRATE},
      {"crf", required_argument, 0, OPT_CRF},
      {"vmaf-target", required_argument, 0, OPT_VMAF_TARGET},
      {"srt", required_argument, 0, OPT_SRT},
      {"help", no_argument, 0, OPT_HELP},
      {"blind", no_argument, 0, OPT_BLIND},
      {"config", no_argument, 0, OPT_CONFIG_SETUP},
      {"dry-run", no_argument, 0, OPT_DRY_RUN},
      {"quiet", no_argument, 0, OPT_QUIET},
      {"verbose", no_argument, 0, OPT_VERBOSE},
      {"grain-only", no_argument, 0, OPT_GRAIN_ONLY},
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
      {"companion-hd", no_argument, 0, OPT_COMPANION_HD},
      {"scale-to-hd", no_argument, 0, OPT_SCALE_TO_HD},
      {"cache-dir", required_argument, 0, OPT_CACHE_DIR},
      {0, 0, 0, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "hb:s:", long_options, NULL)) != -1) {
    switch (opt) {
    case OPT_TMDB:
      tmdb_id = parse_int_or_zero(optarg);
      break;
    case OPT_BITRATE:
      cli_bitrate = parse_int_or_zero(optarg);
      if (cli_bitrate <= 0) {
        fprintf(stderr, "Error: --bitrate must be a positive integer (kbps)\n");
        return 1;
      }
      break;
    case OPT_CRF:
      cli_crf = parse_int_or_zero(optarg);
      if (cli_crf < 1 || cli_crf > 63) {
        fprintf(stderr, "Error: --crf must be in range 1–63\n");
        return 1;
      }
      break;
    case OPT_VMAF_TARGET:
      cli_vmaf_target = parse_int_or_zero(optarg);
      if (cli_vmaf_target < 1 || cli_vmaf_target > 100) {
        fprintf(stderr, "Error: --vmaf-target must be in range 1–100\n");
        return 1;
      }
      break;
    case OPT_SRT:
      if (extra_srt_count < 16)
        extra_srt_paths[extra_srt_count++] = optarg;
      else
        fprintf(stderr, "Warning: too many --srt files, ignoring '%s'\n", optarg);
      break;
    case OPT_HELP:
      print_usage(argv[0]);
      return 0;
    case OPT_BLIND:
      /* Already detected in the pre-scan above; nothing more to do here. */
      break;
    case OPT_CONFIG_SETUP:
      /* Pre-scan dispatches this; reaching here means a flag came before
         it that getopt processed first. Run setup and exit anyway. */
      return config_interactive_setup();
    case OPT_DRY_RUN:
      dry_run = true;
      break;
    case OPT_QUIET:
      quiet = true;
      break;
    case OPT_VERBOSE:
      verbose = true;
      break;
    case OPT_GRAIN_ONLY:
      grain_only = true;
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
    case OPT_COMPANION_HD:
      companion_hd = true;
      break;
    case OPT_SCALE_TO_HD:
      scale_to_hd = true;
      break;
    case OPT_CACHE_DIR:
      snprintf(cli_cache_dir, sizeof(cli_cache_dir), "%s", optarg);
      if (strlen(cli_cache_dir) == 0) {
        fprintf(stderr, "Error: --cache-dir requires a directory path\n");
        return 1;
      }
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
  /* --verbose is orthogonal: it forwards SVT-AV1 chatter to stderr.
     Compatible with --quiet (compact our-output, raw encoder log). */
  if (verbose)
    ui_set_verbose(1);

  if (companion_hd && scale_to_hd) {
    fprintf(stderr, "Error: --companion-hd and --scale-to-hd are mutually exclusive\n");
    return 1;
  }
  int do_hd = companion_hd || scale_to_hd;

  /* ---- Cache directory setup ---- */
  if (strlen(cli_cache_dir) > 0) {
    /* User-provided cache directory */
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s", cli_cache_dir);
  } else {
    /* Default: .vmavificient-cache in project root */
    snprintf(g_cache_dir, sizeof(g_cache_dir), "./.vmavificient-cache");
  }
  if (mkdir(g_cache_dir, 0755) != 0 && errno != EEXIST) {
    char err[128];
    snprintf(err, sizeof(err), "failed to create cache directory '%s' (errno %d)", g_cache_dir,
             errno);
    ui_stage_fail("Cache", err);
    return 1;
  }

  const char *filepath = NULL;
  if (optind < argc)
    filepath = argv[optind];
  else
    filepath = DEFAULT_TEST_FILE;

  /* Cache scores file path */
  char scores_cache_path[4096];
  snprintf(scores_cache_path, sizeof(scores_cache_path), "%s/scores.json", g_cache_dir);

  /* Score cache state - used for grain analysis and CRF caching */
  double cached_grain_score = 0.0;
  double cached_grain_variance = 0.0;
  int cached_crf = 0;
  bool scores_cached = false;

  /* GrainScore struct needed later for grain analysis and HD encode */
  GrainScore grain = {0};

  /* ---- Source ---- */
  MediaInfo info = get_media_info(filepath);
  if (info.error != 0) {
    char err[64];
    snprintf(err, sizeof(err), "could not probe %s (error %d)", filepath, info.error);
    ui_stage_fail("Source probe", err);
    ui_hint("verify the path and that ffmpeg can read the container");
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
      ui_kv("Dolby Vision", "yes  (profile %d, level %d)", hdr.dv_profile, hdr.dv_level);
    else
      ui_kv("Dolby Vision", "no");
    ui_kv("HDR10+", "%s", hdr.has_hdr10plus ? "yes" : "no");
  }

  /* ---- Crop ---- */
  CropInfo crop = get_crop_info(filepath);
  if (crop.error == 0 && (crop.top || crop.bottom || crop.left || crop.right)) {
    ui_section("Crop");
    ui_kv("Detected", "T/B %d/%d   L/R %d/%d", crop.top, crop.bottom, crop.left, crop.right);
  }

  /* ---- Tracks ---- */
  MediaTracks tracks = get_media_tracks(filepath);
  TrackInfo *best = NULL;
  int best_count = 0;
  if (tracks.error == 0) {
    ui_section("Tracks");

    int split_fre = (cli_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;
    best = select_best_audio_per_language(&tracks, split_fre, &best_count);

    /* ── Audio table ──────────────────────────────────────────────────────
     * Columns: #(2) lng(3) codec(7) ch(3) bitrate(10) title(20)
     * "→" marker (3 UTF-8 bytes, 1 display col) + space sits outside the
     * left border so the │ stays at the same column for all rows.       */
    ui_kv("Audio", "%d source track%s", tracks.audio_count, tracks.audio_count == 1 ? "" : "s");
    // clang-format off
    ui_row("  \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90");
    ui_row("  \xe2\x94\x82 %2s \xe2\x94\x82 %-3s \xe2\x94\x82 %-7s \xe2\x94\x82 %-3s \xe2\x94\x82 %10s \xe2\x94\x82 %-20s \xe2\x94\x82",
           " #", "lng", "codec", " ch", "   bitrate", "title");
    ui_row("  \xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xa4");
    // clang-format on
    for (int i = 0; i < tracks.audio_count; i++) {
      long long kbps = (long long)(tracks.audio[i].bitrate / 1000);
      char rate_buf[16];
      if (kbps > 0)
        snprintf(rate_buf, sizeof(rate_buf), "%5lld kbps", kbps);
      else
        snprintf(rate_buf, sizeof(rate_buf), "          ");
      char ch_buf[8];
      snprintf(ch_buf, sizeof(ch_buf), "%dch", tracks.audio[i].channels);
      bool sel = false;
      for (int j = 0; j < best_count && !sel; j++)
        sel = (best[j].index == tracks.audio[i].index);
      // clang-format off
      ui_row(
          "%s\xe2\x94\x82 %2d \xe2\x94\x82 %-3s \xe2\x94\x82 %-7.7s \xe2\x94\x82 %-3s \xe2\x94\x82 %s \xe2\x94\x82 %-20.20s \xe2\x94\x82",
          sel ? "\xe2\x86\x92 " : "  ", tracks.audio[i].index,
          tracks.audio[i].language, tracks.audio[i].codec, ch_buf, rate_buf,
          tracks.audio[i].name);
      // clang-format on
    }
    // clang-format off
    ui_row("  \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98");
    // clang-format on

    if (best && best_count > 0) {
      char idx_buf[64] = "";
      size_t pos = 0;
      for (int i = 0; i < best_count; i++) {
        int n =
            snprintf(idx_buf + pos, sizeof(idx_buf) - pos, i > 0 ? "  #%d" : "#%d", best[i].index);
        if (n > 0 && (size_t)n < sizeof(idx_buf) - pos)
          pos += (size_t)n;
      }
      ui_kv("Selected", "%d track%s for encode  (%s)", best_count, best_count == 1 ? "" : "s",
            idx_buf);
    }

    /* ── Subtitle table ───────────────────────────────────────────────────
     * Columns: #(2) lng(3) fmt(3) type(6) title(20)                     */
    int n_karaoke = 0;
    for (int i = 0; i < tracks.subtitle_count; i++)
      if (tracks.subtitles[i].is_karaoke)
        n_karaoke++;
    int n_visible = tracks.subtitle_count - n_karaoke;
    if (n_karaoke > 0)
      ui_kv("Subtitles", "%d source track%s  (%d karaoke excluded)", n_visible,
            n_visible == 1 ? "" : "s", n_karaoke);
    else
      ui_kv("Subtitles", "%d source track%s", tracks.subtitle_count,
            tracks.subtitle_count == 1 ? "" : "s");

    // Pre-compute which PGS subtitles will be skipped (already have text SRT)
    // Two-pass: first collect all SRT tracks, then mark PGS as skipped
    bool pgs_skipped[256] = {0}; // max 256 subtitle tracks
    int pgs_skipped_count = 0;

    // Track SRTs we've seen: (lang, variant, forced, sdh) for skip detection
    int srt_seen_count = 0;
    char srt_seen_lang[64][64];
    int srt_seen_variant[64]; // FRENCH_VARIANT_* or 0 for non-French
    int srt_seen_forced[64];
    int srt_seen_sdh[64];

    // PASS 1: Collect all SRT tracks
    for (int i = 0; i < tracks.subtitle_count; i++) {
      TrackInfo *sub = &tracks.subtitles[i];
      if (sub->is_karaoke)
        continue;
      const char *lang = sub->language[0] ? sub->language : "und";

      if (is_text_subtitle(sub)) {
        if (srt_seen_count < 64) {
          snprintf(srt_seen_lang[srt_seen_count], sizeof(srt_seen_lang[0]), "%s", lang);
          srt_seen_variant[srt_seen_count] = detect_track_french_variant(sub);
          srt_seen_forced[srt_seen_count] = sub->is_forced;
          srt_seen_sdh[srt_seen_count] = sub->is_sdh;
          srt_seen_count++;
        }
      }
    }

    // PASS 2: Mark PGS subtitles that have matching SRT (anywhere in list)
    for (int i = 0; i < tracks.subtitle_count; i++) {
      TrackInfo *sub = &tracks.subtitles[i];
      if (sub->is_karaoke)
        continue;

      if (is_pgs_subtitle(sub)) {
        const char *lang = sub->language[0] ? sub->language : "und";
        int pgs_variant = detect_track_french_variant(sub);
        for (int j = 0; j < srt_seen_count; j++) {
          if (strcmp(srt_seen_lang[j], lang) == 0 && srt_seen_variant[j] == pgs_variant &&
              srt_seen_forced[j] == sub->is_forced && srt_seen_sdh[j] == sub->is_sdh) {
            pgs_skipped[sub->index] = true;
            pgs_skipped_count++;
            break;
          }
        }
      }
    }

    if (n_visible > 0) {
      // clang-format off
      ui_row("  \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90");
      ui_row("  \xe2\x94\x82 %2s \xe2\x94\x82 %-3s \xe2\x94\x82 %-3s \xe2\x94\x82 %-6s \xe2\x94\x82 %-20s \xe2\x94\x82",
             " #", "lng", "fmt", " type", "title");
      ui_row("  \xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xa4");
      // clang-format on
      for (int i = 0; i < tracks.subtitle_count; i++) {
        const char *type = tracks.subtitles[i].is_forced ? "forced"
                           : tracks.subtitles[i].is_sdh  ? "sdh"
                                                         : "full";
        const char *lang = tracks.subtitles[i].language[0] ? tracks.subtitles[i].language : "und";
        const char *selection = "  ";

        // Determine selection marker
        if (tracks.subtitles[i].is_karaoke) {
          // Karaoke tracks: Not selected (×)
          selection = "\xc3\x97 ";
        } else if (is_text_subtitle(&tracks.subtitles[i])) {
          // Text SRT tracks: Selected for direct extraction (→)
          selection = "\xe2\x86\x92 ";
        } else if (is_pgs_subtitle(&tracks.subtitles[i])) {
          // PGS tracks: check if skipped or needs OCR
          if (pgs_skipped[tracks.subtitles[i].index]) {
            // Has matching SRT - not selected (×)
            selection = "\xc3\x97 ";
          } else {
            // Needs OCR conversion (O)
            selection = "O ";
          }
        }
        // clang-format off
        ui_row(
            "%s\xe2\x94\x82 %2d \xe2\x94\x82 %-3s \xe2\x94\x82 %-3s \xe2\x94\x82 %-6s \xe2\x94\x82 %-20.20s \xe2\x94\x82",
            selection, tracks.subtitles[i].index, lang,
            codec_short(tracks.subtitles[i].codec), type,
            tracks.subtitles[i].name);
        // clang-format on
      }
      // clang-format off
      ui_row("  \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98");
      // clang-format on

      // Count subtitle types for display:
      // - text_count: SRT tracks (selected for direct extraction)
      // - pgs_count: PGS tracks that need OCR (not skipped)
      int text_count = 0, pgs_ocr_count = 0;
      for (int i = 0; i < tracks.subtitle_count; i++) {
        if (tracks.subtitles[i].is_karaoke)
          continue;
        if (is_text_subtitle(&tracks.subtitles[i]))
          text_count++;
        else if (is_pgs_subtitle(&tracks.subtitles[i]) && !pgs_skipped[tracks.subtitles[i].index])
          pgs_ocr_count++;
      }
      if (text_count > 0 || pgs_ocr_count > 0) {
        char detail[128] = "";
        size_t pos = 0;
        if (text_count > 0) {
          int n = snprintf(detail + pos, sizeof(detail) - pos, "%d direct SRT", text_count);
          if (n > 0 && (size_t)n < sizeof(detail) - pos)
            pos += (size_t)n;
        }
        if (pgs_ocr_count > 0) {
          int n = snprintf(detail + pos, sizeof(detail) - pos,
                           pos > 0 ? ", %d OCR PGS" : "%d OCR PGS", pgs_ocr_count);
          if (n > 0 && (size_t)n < sizeof(detail) - pos)
            pos += (size_t)n;
          if (pgs_skipped_count > 0) {
            n = snprintf(detail + pos, sizeof(detail) - pos, " (%d already available)",
                         pgs_skipped_count);
            if (n > 0 && (size_t)n < sizeof(detail) - pos)
              pos += (size_t)n;
          }
        }
        ui_kv("Processing", "%s", detail);
      }
    }
  }

  /* Early resume check: if .video.mkv already exists, skip encoding */
  char video_4k_cache_path[4096] = "";
  char video_hd_cache_path[4096] = "";

  /* Compute base name from filepath for cache lookup */
  char base_for_cache[1024];
  const char *fname = strrchr(filepath, '/');
  fname = fname ? fname + 1 : filepath;
  snprintf(base_for_cache, sizeof(base_for_cache), "%s", fname);
  char *ext = strrchr(base_for_cache, '.');
  if (ext)
    *ext = '\0';

  snprintf(video_4k_cache_path, sizeof(video_4k_cache_path), "%s/%s.video.mkv", g_cache_dir,
           base_for_cache);
  if (do_hd && !scale_to_hd) {
    snprintf(video_hd_cache_path, sizeof(video_hd_cache_path), "%s/%s-HDLight.video.mkv",
             g_cache_dir, base_for_cache);
  } else {
    snprintf(video_hd_cache_path, sizeof(video_hd_cache_path), "%s/%s.video.mkv", g_cache_dir,
             base_for_cache);
  }

  if (file_exists(video_4k_cache_path)) {
    ui_section("Resume check");
    ui_stage_ok("skip", "4K video.mkv already present in cache, proceeding to mux");
    return 0;
  }

  if (file_exists(video_hd_cache_path)) {
    ui_section("Resume check");
    ui_stage_ok("skip", "HD video.mkv already present in cache, proceeding to mux");
    return 0;
  }

  /* ---- Grain analysis ---- */
  ui_section("Grain analysis");
  int film_grain = 0;

  /* Check for cached scores (uses global cached_* variables) */
  scores_cached = load_cached_scores(scores_cache_path, &cached_grain_score, &cached_grain_variance,
                                     &cached_crf);

  if (scores_cached) {
    film_grain = get_film_grain_from_score(cached_grain_score, cached_grain_variance, cli_quality);
    ui_kv("Luma score", "%.4f  (from cache)", cached_grain_score);
    ui_kv("Chroma score", "%.4f", cached_grain_variance * 100); /* approximate */
    ui_kv("Variance", "%.4f  (from cache)", cached_grain_variance);
    ui_kv("Synth level", "%d  (from cache)", film_grain);
    ui_stage_ok("Grain analysis", "loaded from cache");
    /* Refresh timestamp when using cached scores */
    if (!cli_crf && !cli_bitrate) {
      save_cached_scores(scores_cache_path, cached_grain_score, cached_grain_variance, cached_crf);
    }
  } else {
    ui_row("Sampling 4 windows (extends to 7 if variance is high)…");
    grain = get_grain_score(filepath);
    if (grain.error == 0) {
      film_grain = get_film_grain_from_score(grain.grain_score, grain.grain_variance, cli_quality);
      /* Per-window OK/FAIL lines already printed by media_analysis as it
         worked; these summary kvs collect the aggregate signal. */
      ui_kv("Luma score", "%.4f  (max across windows)", grain.grain_score);
      ui_kv("Chroma score", "%.4f", grain.grain_score * 100); /* approximate */
      ui_kv("Variance", "%.4f%s", grain.grain_variance,
            grain.windows_succeeded > 4 ? "  (refinement triggered)" : "");
      ui_kv("Synth level", "%d  (0–50)", film_grain);
      /* Save grain scores to cache */
      if (!cli_crf && !cli_bitrate) {
        if (!save_cached_scores(scores_cache_path, grain.grain_score, grain.grain_variance, 0)) {
          fprintf(stderr, "Warning: failed to save scores to cache\n");
        } else {
          ui_stage_ok("Cache", "saved grain analysis scores");
        }
      }
    } else {
      char err[64];
      snprintf(err, sizeof(err), "all windows failed (last error %d)", grain.error);
      ui_stage_fail("Grain analysis", err);
      ui_hint("set VMAV_KEEP_GRAIN_TMP=1 to retain the per-window scratch "
              "files for inspection");
    }
  }

  const EncodePreset *enc_preset = get_encode_preset(cli_quality, info.height);

  if (do_hd && info.height < 2160) {
    ui_stage_fail("Source", "--companion-hd / --scale-to-hd requires a 4K source "
                            "(height >= 2160)");
    return 1;
  }

  /* ---- Naming setup (TMDB or --blind) ---- */
  char output_name[1024] = "";
  char base_name[1024] = "";
  char output_dir[2048] = "";
  char mkv_title[1024] = "";
  /* Saved for HD companion naming (filled by TMDB branch below). */
  char saved_tmdb_title[512] = "";
  int saved_tmdb_year = 0;
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

    if (tracks.error == 0 && tracks.audio_count > 0 && tracks.audio[0].language[0])
      video_language = tracks.audio[0].language;

    naming_ok = true;
  } else if (tmdb_id > 0) {
    ui_section("TMDB lookup");
    ui_kv("Movie ID", "%d", tmdb_id);
    TmdbMovieInfo tmdb = tmdb_fetch_movie(tmdb_id);
    if (tmdb.error != 0) {
      ui_stage_fail("TMDB fetch", "could not fetch movie info");
      ui_hint("verify TMDB_API_KEY is set in config.ini and the ID is "
              "correct (e.g. tmdb.org/movie/<id>)");
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
        LanguageTag auto_tag = determine_language_tag(&tracks, tmdb.original_language, fv);

        /* If auto-detection produced a definitive result, use it.
           Otherwise ask the user interactively. */
        if (auto_tag != LANG_TAG_VO || tracks.audio_count <= 1) {
          lang_tag = auto_tag;
        } else {
          lang_tag = ask_language_tag(&tracks);
        }
      }
      resolved_lang_tag = lang_tag;

      snprintf(saved_tmdb_title, sizeof(saved_tmdb_title), "%s", tmdb.original_title);
      saved_tmdb_year = tmdb.release_year;

      build_output_filename(output_name, sizeof(output_name), tmdb.original_title,
                            tmdb.release_year, lang_tag, &info, &hdr, source);

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

      snprintf(mkv_title, sizeof(mkv_title), "%s (%d)", tmdb.original_title, tmdb.release_year);
      {
        const char *vlang = iso639_1_to_2b(tmdb.original_language);
        if (vlang)
          video_language = vlang;
      }

      naming_ok = true;
    }
  }

  if (naming_ok) {
    /* ---- Rate control: CRF search or manual override ---- */
    int crf = cli_crf;         /* 0 if not specified */
    int bitrate = cli_bitrate; /* 0 if not specified; VBR only when set */
    int vmaf_used = 0;

    if (!scale_to_hd) {
      /* ---- 4K rate control + plan ---- */
      if (!grain_only && crf == 0 && bitrate == 0) {
        /* Check for cached CRF before running search */
        if (scores_cached && cached_crf > 0) {
          crf = cached_crf;
          vmaf_used =
              cli_vmaf_target > 0 ? cli_vmaf_target : get_vmaf_target(cli_quality, info.height);
          ui_section("CRF search");
          char detail[64];
          snprintf(detail, sizeof(detail), "using cached CRF %d", crf);
          ui_stage_ok("skip", detail);
        } else {
          /* Default path: probe CRF via ab-av1 at source (4K) resolution. */
          vmaf_used =
              cli_vmaf_target > 0 ? cli_vmaf_target : get_vmaf_target(cli_quality, info.height);
          ui_section("CRF search");
          CrfSearchResult csr = run_crf_search(filepath, vmaf_used, enc_preset, film_grain, NULL);
          if (csr.ab_av1_missing) {
            ui_stage_fail("crf-search", "ab-av1 not found in PATH");
            ui_hint("install: brew install master-of-mint/tap/ab-av1  "
                    "or bypass with --crf <N> or --bitrate <kbps>");
            if (tracks.error == 0)
              free_media_tracks(&tracks);
            return 1;
          }
          if (csr.crf < 0) {
            ui_stage_fail("crf-search", "ab-av1 exited with error or no CRF parsed");
            ui_hint("if params changed since last run, clear stale cache: "
                    "rm -rf ~/.cache/ab-av1/");
            ui_hint("bypass with --crf <N> or --bitrate <kbps>");
            if (tracks.error == 0)
              free_media_tracks(&tracks);
            return 1;
          }
          char detail[64];
          snprintf(detail, sizeof(detail), "CRF %d  (VMAF target %d)", csr.crf, vmaf_used);
          ui_stage_ok("crf-search", detail);
          crf = csr.crf;
        }
      } else if (cli_vmaf_target > 0) {
        vmaf_used = cli_vmaf_target;
      }

      /* Plan + Dry-run notice always render. */
      int saved_quiet = ui_is_quiet();
      ui_set_quiet(0);
      ui_section("Encoding plan");
      ui_kv("Preset", "%s  (%s)", quality_type_to_string(cli_quality),
            info.height >= 2160 ? "4K" : "HD");
      ui_kv("SVT-AV1", "preset %d, tune %d, keyint %d, ac-bias %.1f", enc_preset->preset,
            enc_preset->tune, enc_preset->keyint, enc_preset->ac_bias);
      if (grain.error == 0) {
        int is_anim = (cli_quality == QUALITY_ANIMATION);
        const char *content_tier =
            is_anim ? "animation" : (grain.grain_score >= 0.08 ? "grainy" : "clean");
        ui_kv("Grain", "level %d  (%s tier)", film_grain, content_tier);
      }
      if (crf > 0) {
        if (vmaf_used > 0)
          ui_kv("CRF", "%d  (VMAF target %d)", crf, vmaf_used);
        else
          ui_kv("CRF", "%d  (manual)", crf);
      } else if (bitrate > 0) {
        ui_kv("Bitrate", "%d kbps VBR  (manual)", bitrate);
      } else {
        ui_kv("CRF", "(use --dry-run to probe, or --crf <N> to set)");
      }
      ui_kv("Output", "%s%s", output_dir, output_name);

      if (grain_only)
        print_encoder_knobs(enc_preset, film_grain);

      /* For companion-hd, fall through so the HD plan section also renders
         before exiting.  For solo dry-run / grain-only, exit here. */
      if ((dry_run || grain_only) && !companion_hd) {
        ui_section(grain_only ? "Grain-only" : "Dry run");
        ui_row("No files written. Re-run without %s to encode.",
               grain_only ? "--grain-only" : "--dry-run");
        if (tracks.error == 0)
          free_media_tracks(&tracks);
        return 0;
      }
      ui_set_quiet(saved_quiet);
    } /* !scale_to_hd */

    {
      /* ---- OPUS audio encoding ---- */
      int enc_best_count = 0;
      int enc_split_fre = (resolved_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;
      TrackInfo *enc_best = select_best_audio_per_language(&tracks, enc_split_fre, &enc_best_count);

      /* Sort: French first, then English, then others */
      if (enc_best && enc_best_count > 1)
        qsort(enc_best, enc_best_count, sizeof(TrackInfo), cmp_audio_order);

      /* Store OPUS paths and track names for final mux */
      char opus_paths[32][4096];
      char audio_names[32][256];
      int opus_count = 0;

      if (enc_best && enc_best_count > 0 && !dry_run && !grain_only) {
        ui_section("Audio");
        ui_kv("Encode", "%d track%s → OPUS", enc_best_count, enc_best_count == 1 ? "" : "s");
        for (int i = 0; i < enc_best_count && i < 32; i++) {
          /* In VF2 mode, each French track gets its own variant derived from
             the track title ("VFF - DTS-HD…", "VFQ - E-AC3…") so VFF and VFQ
             produce distinct .fre.fr.opus / .fre.ca.opus files. */
          FrenchVariant track_fv = fv;
          FrenchAudioOrigin track_origin = fr_audio_origin;
          if (enc_split_fre && (strcmp(enc_best[i].language, "fre") == 0 ||
                                strcmp(enc_best[i].language, "fra") == 0)) {
            FrenchVariant detected = (FrenchVariant)detect_track_french_variant(&enc_best[i]);
            if (detected != FRENCH_VARIANT_UNKNOWN) {
              track_fv = detected;
              track_origin = (detected == FRENCH_VARIANT_VFQ)   ? FRENCH_AUDIO_VFQ
                             : (detected == FRENCH_VARIANT_VFI) ? FRENCH_AUDIO_VFI
                                                                : FRENCH_AUDIO_VFF;
            }
          }

          char opus_name[2048];
          build_opus_filename(opus_name, sizeof(opus_name), base_name, enc_best[i].language,
                              track_fv);

          /* Write opus to cache directory */
          char opus_cache_path[4096];
          build_cache_path(opus_cache_path, sizeof(opus_cache_path), opus_name);
          snprintf(opus_paths[i], sizeof(opus_paths[i]), "%s", opus_cache_path);

          /* Build display name for MKV track */
          build_audio_track_name(audio_names[i], sizeof(audio_names[i]), enc_best[i].language,
                                 enc_best[i].channels, track_origin);

          ui_row("[%d/%d] %s  %s  %dch  %lld kbps  →  \"%s\"", i + 1, enc_best_count,
                 enc_best[i].language, enc_best[i].codec, enc_best[i].channels,
                 (long long)(enc_best[i].bitrate / 1000), audio_names[i]);

          time_t track_t0 = time(NULL);
          OpusEncodeResult r = encode_track_to_opus(filepath, &enc_best[i], opus_paths[i]);

          if (r.skipped) {
            ui_stage_skip(opus_name, "already exists");
          } else if (r.error == 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%s", ui_fmt_duration(difftime(time(NULL), track_t0)));
            ui_stage_ok(opus_name, detail);
          } else {
            char err[128];
            snprintf(err, sizeof(err), "stream #%d (%s, %dch): error %d", enc_best[i].index,
                     enc_best[i].codec, enc_best[i].channels, r.error);
            ui_stage_fail(opus_name, err);
            ui_hint("verify the source stream is decodable; opusenc-style "
                    "channel layouts (>2ch) require ffmpeg with libopus");
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
      int srt_variant[64]; /* FrenchVariant per track (0 = unknown/non-French)
                            */
      int srt_count = 0;
      int sub_split_fre = (resolved_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;

      if (tracks.error == 0 && tracks.subtitle_count > 0 && !dry_run && !grain_only) {
        ui_section("Subtitles");
        int n_sub_process = 0;
        for (int i = 0; i < tracks.subtitle_count; i++)
          if (!tracks.subtitles[i].is_karaoke)
            n_sub_process++;
        ui_kv("Process", "%d source track%s", n_sub_process, n_sub_process == 1 ? "" : "s");

        for (int i = 0; i < tracks.subtitle_count && srt_count < 48; i++) {
          TrackInfo *sub = &tracks.subtitles[i];
          if (sub->is_karaoke)
            continue;
          const char *lang = sub->language[0] ? sub->language : "und";

          /* Per-track French variant so VF2 sources keep VFF and VFQ
             subtitles as separate .fre.fr.srt / .fre.ca.srt files. */
          FrenchVariant track_fv = fv;
          FrenchAudioOrigin track_origin = fr_audio_origin;
          int track_variant_key = 0;
          if (sub_split_fre && (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0)) {
            FrenchVariant detected = (FrenchVariant)detect_track_french_variant(sub);
            if (detected != FRENCH_VARIANT_UNKNOWN) {
              track_fv = detected;
              track_variant_key = (int)detected;
              track_origin = (detected == FRENCH_VARIANT_VFQ)   ? FRENCH_AUDIO_VFQ
                             : (detected == FRENCH_VARIANT_VFI) ? FRENCH_AUDIO_VFI
                                                                : FRENCH_AUDIO_VFF;
            }
          }

          if (is_text_subtitle(sub)) {
            /* Text subtitle: extract directly to SRT via FFmpeg CLI */
            char srt_fname[2048];
            build_srt_filename(srt_fname, sizeof(srt_fname), base_name, lang, track_fv,
                               sub->is_forced, sub->is_sdh);

            /* Write SRT to cache directory */
            char srt_cache_path[4096];
            build_cache_path(srt_cache_path, sizeof(srt_cache_path), srt_fname);
            snprintf(srt_paths[srt_count], sizeof(srt_paths[0]), "%s", srt_cache_path);

            /* Build display name */
            build_subtitle_track_name(srt_names[srt_count], sizeof(srt_names[0]), lang, 1,
                                      sub->is_forced, sub->is_sdh, track_origin);
            snprintf(srt_langs[srt_count], sizeof(srt_langs[0]), "%s", lang);
            srt_is_forced[srt_count] = sub->is_forced;
            srt_is_sdh[srt_count] = sub->is_sdh;
            srt_variant[srt_count] = track_variant_key;

            /* Check if SRT already exists */
            struct stat srt_st;
            if (stat(srt_paths[srt_count], &srt_st) == 0 && srt_st.st_size > 0) {
              ui_stage_skip(srt_fname, "already exists");
              srt_count++;
            } else {
              /* Extract text subtitle using ffmpeg command */
              char cmd[8192];
              /* If already SRT (subrip), copy stream; else convert to srt. */
              const char *codec_arg = (strcmp(sub->codec, "subrip") == 0) ? "copy" : "srt";
              /* Build the command with shell_quote_append() for paths so
                 a filename containing $(…), backticks, or quotes can't
                 escape into the shell. */
              size_t pos = 0;
              int n = snprintf(cmd, sizeof(cmd), "ffmpeg -y -loglevel error -i ");
              if (n > 0)
                pos = (size_t)n;
              shell_quote_append(cmd, sizeof(cmd), &pos, filepath);
              n = snprintf(cmd + pos, sizeof(cmd) - pos, " -map 0:%d -c:s %s ", sub->index,
                           codec_arg);
              if (n > 0 && (size_t)n < sizeof(cmd) - pos)
                pos += (size_t)n;
              shell_quote_append(cmd, sizeof(cmd), &pos, srt_paths[srt_count]);

              ui_row("Extract  #%-2d  %s  %s  →  \"%s\"", sub->index, lang, sub->codec,
                     srt_names[srt_count]);

              int rc = system(cmd);
              int exit_code = (rc == -1 || !WIFEXITED(rc)) ? -1 : WEXITSTATUS(rc);
              if (exit_code == 0) {
                ui_stage_ok(srt_fname, NULL);
                srt_count++;
              } else {
                char err[128];
                snprintf(err, sizeof(err), "stream #%d (%s, %s): ffmpeg rc=%d", sub->index, lang,
                         sub->codec, exit_code);
                ui_stage_fail("Subtitle extraction", err);
                ui_hint("verify ffmpeg is on PATH and the stream codec is "
                        "convertible to subrip");
              }
            }
          } else if (is_pgs_subtitle(sub)) {
            /* PGS bitmap subtitle: OCR with Tesseract */

            /* Check if a text SRT already exists for this (lang, variant,
               forced, sdh) — dedup is per variant so a VF2 source won't
               drop a VFQ PGS when only a VFF SRT is present. */
            bool srt_exists_for_lang = false;
            for (int j = 0; j < srt_count; j++) {
              if (strcmp(srt_langs[j], lang) == 0 && srt_variant[j] == track_variant_key &&
                  srt_is_forced[j] == sub->is_forced && srt_is_sdh[j] == sub->is_sdh) {
                srt_exists_for_lang = true;
                break;
              }
            }

            if (srt_exists_for_lang) {
              char skip_label[64];
              snprintf(skip_label, sizeof(skip_label), "PGS #%d %s", sub->index, lang);
              ui_stage_skip(skip_label, "SRT already available");
              continue;
            }

            char srt_fname[2048];
            build_srt_filename(srt_fname, sizeof(srt_fname), base_name, lang, track_fv,
                               sub->is_forced, sub->is_sdh);

            /* Write SRT to cache directory */
            char srt_cache_path[4096];
            build_cache_path(srt_cache_path, sizeof(srt_cache_path), srt_fname);
            snprintf(srt_paths[srt_count], sizeof(srt_paths[0]), "%s", srt_cache_path);

            build_subtitle_track_name(srt_names[srt_count], sizeof(srt_names[0]), lang, 1,
                                      sub->is_forced, sub->is_sdh, track_origin);
            snprintf(srt_langs[srt_count], sizeof(srt_langs[0]), "%s", lang);
            srt_is_forced[srt_count] = sub->is_forced;
            srt_is_sdh[srt_count] = sub->is_sdh;
            srt_variant[srt_count] = track_variant_key;

            ui_row("OCR      PGS #%-2d  %s  →  \"%s\"", sub->index, lang, srt_names[srt_count]);

            SubtitleConvertResult scr =
                convert_pgs_to_srt(filepath, sub, srt_paths[srt_count], NULL);

            if (scr.skipped) {
              ui_stage_skip(srt_fname, "already exists");
              srt_count++;
            } else if (scr.error == 0 && scr.subtitle_count > 0) {
              char detail[64];
              snprintf(detail, sizeof(detail), "%d subtitles", scr.subtitle_count);
              ui_stage_ok(srt_fname, detail);
              srt_count++;
            } else if (scr.error == 0) {
              ui_stage_skip(srt_fname, "no subtitles extracted");
            } else {
              char err[128];
              snprintf(err, sizeof(err), "PGS #%d (%s): OCR error %d", sub->index, lang, scr.error);
              ui_stage_fail(srt_fname, err);
              ui_hint("verify Tesseract has training data for the "
                      "language ($TESSDATA_PREFIX/<lang>.traineddata)");
            }
          }
        }
      }

      /* ---- Add user-supplied SRT files ---- */
      for (int i = 0; i < extra_srt_count && srt_count < 64; i++) {
        snprintf(srt_paths[srt_count], sizeof(srt_paths[0]), "%s", extra_srt_paths[i]);

        /* Try to guess language from filename */
        const char *srt_lang = "und";
        if (strstr(extra_srt_paths[i], ".fre.") || strstr(extra_srt_paths[i], ".fra."))
          srt_lang = "fre";
        else if (strstr(extra_srt_paths[i], ".eng."))
          srt_lang = "eng";
        else if (strstr(extra_srt_paths[i], ".ger.") || strstr(extra_srt_paths[i], ".deu."))
          srt_lang = "ger";
        else if (strstr(extra_srt_paths[i], ".spa."))
          srt_lang = "spa";
        else if (strstr(extra_srt_paths[i], ".ita."))
          srt_lang = "ita";

        int forced = (strstr(extra_srt_paths[i], "forced") != NULL) ? 1 : 0;
        int sdh =
            (strstr(extra_srt_paths[i], "sdh") != NULL || strstr(extra_srt_paths[i], "SDH") != NULL)
                ? 1
                : 0;

        build_subtitle_track_name(srt_names[srt_count], sizeof(srt_names[0]), srt_lang, 1, forced,
                                  sdh, fr_audio_origin);
        snprintf(srt_langs[srt_count], sizeof(srt_langs[0]), "%s", srt_lang);
        srt_is_forced[srt_count] = forced;
        srt_is_sdh[srt_count] = sdh;
        srt_variant[srt_count] = 0;

        printf("  [SRT]  %s → \"%s\"\n", extra_srt_paths[i], srt_names[srt_count]);
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
      if (!scale_to_hd && !dry_run && !grain_only) { /* 4K encode block */
        if (hdr.error == 0 && hdr.has_dolby_vision) {
          char rpu_name[2048];
          build_rpu_filename(rpu_name, sizeof(rpu_name), base_name);

          /* Write RPU to cache directory */
          char rpu_cache_path[4096];
          build_cache_path(rpu_cache_path, sizeof(rpu_cache_path), rpu_name);
          snprintf(rpu_path, sizeof(rpu_path), "%s", rpu_cache_path);

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
            ui_hint("source claims Dolby Vision but RPU extraction failed; "
                    "encode will continue without DV metadata");
            rpu_path[0] = '\0'; /* Don't use RPU on failure */
          }
        }

        /* ---- AV1 video encoding ---- */
        char av1_video_name[2048];
        snprintf(av1_video_name, sizeof(av1_video_name), "%s.video.mkv", base_name);
        char av1_video_cache_path[4096];
        build_cache_path(av1_video_cache_path, sizeof(av1_video_cache_path), av1_video_name);
        char av1_video_path[4096];
        snprintf(av1_video_path, sizeof(av1_video_path), "%s", av1_video_cache_path);
        time_t video_t0 = time(NULL);
        VideoEncodeResult vr = {0};

        {
          ui_section("Video encoding");

          VideoEncodeConfig vcfg = {
              .input_path = filepath,
              .output_path = av1_video_path,
              .rpu_path = rpu_path[0] ? rpu_path : NULL,
              .preset = enc_preset,
              .film_grain = film_grain,
              .grain_score = grain.error == 0 ? grain.grain_score : 0.0,
              .grain_variance = grain.error == 0 ? grain.grain_variance : 0.0,
              .target_bitrate = bitrate,
              .crf = crf,
              .info = &info,
              .crop = (crop.error == 0) ? &crop : NULL,
              .hdr = &hdr,
          };

          vr = encode_video(&vcfg);

          if (vr.skipped) {
            ui_stage_skip("video.mkv", "already exists");
          } else if (vr.error == 0) {
            char detail[128];
            snprintf(detail, sizeof(detail), "%lld frames, %s in %s", (long long)vr.frames_encoded,
                     ui_fmt_bytes(vr.bytes_written),
                     ui_fmt_duration(difftime(time(NULL), video_t0)));
            ui_stage_ok("video.mkv", detail);
          } else {
            char err[64];
            snprintf(err, sizeof(err), "error %d after %lld frames", vr.error,
                     (long long)vr.frames_encoded);
            ui_stage_fail("video.mkv", err);
            ui_hint("re-run with --verbose to forward SVT-AV1's own log to "
                    "stderr (rate control, GOP layout, fatal warnings)");
          }
        }

        /* ---- Final MKV muxing ---- */
        {
          char final_path[4096];
          snprintf(final_path, sizeof(final_path), "%s%s", output_dir, output_name);

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
                (strcmp(srt_langs[i], "fre") == 0 || strcmp(srt_langs[i], "fra") == 0)) {
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
          ui_kv("Inputs", "1 video + %d audio + %d subtitle track%s", opus_count, srt_count,
                srt_count == 1 ? "" : "s");
          time_t mux_t0 = time(NULL);
          FinalMuxResult mr = final_mux(&mux_cfg);

          if (mr.skipped) {
            ui_stage_skip(output_name, "already exists");
          } else if (mr.error == 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%s", ui_fmt_duration(difftime(time(NULL), mux_t0)));
            ui_stage_ok(output_name, detail);
          } else {
            char err[128];
            snprintf(err, sizeof(err), "error %d (%d audio + %d sub inputs)", mr.error, opus_count,
                     srt_count);
            ui_stage_fail(output_name, err);
            ui_hint("intermediates kept on disk; inspect them next to the "
                    "source file before re-running");
          }

          /* Clean up intermediate files on success. Leaving sidecar .srt
             files next to the final MKV causes players to auto-load them
             as external subtitles, overriding the embedded defaults. */
          int removed = 0;
          if (mr.error == 0) {
            /* For --companion-hd and --scale-to-hd, audio/subtitle intermediates
               are reused by the HD mux; defer their removal to the HD cleanup pass.
               Only the 4K video.mkv and RPU (if not DV) are removed here. */
            if (!companion_hd) {
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
            }
            if (remove(av1_video_path) == 0)
              removed++;
            /* For --companion-hd the RPU is reused by the HD encode;
               defer deletion to the HD cleanup pass. */
            if (rpu_path[0] && !companion_hd && remove(rpu_path) == 0)
              removed++;
            if (removed > 0) {
              char detail[64];
              snprintf(detail, sizeof(detail), "%d intermediate file%s", removed,
                       removed == 1 ? "" : "s");
              ui_stage_ok("Cleanup", detail);
            }
            /* Clean up cache directory when everything succeeded
             * For companion-hd, defer cleanup to the HD mux pass (audio/subtitle
             * intermediates are shared between 4K and HD muxes). */
            if (mr.error == 0 && !companion_hd)
              cleanup_cache_dir();
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
              avg_kbps = ((double)final_bytes * 8.0) / (info.duration * 1000.0);
            double delta_pct = bitrate > 0 ? (avg_kbps - bitrate) / bitrate * 100.0 : 0.0;
            double speed = elapsed > 0.5 ? info.duration / elapsed : 0.0;

            /* Done receipt always renders — it's the headline result. */
            int saved_quiet_done = ui_is_quiet();
            ui_set_quiet(0);
            ui_section("Done");
            ui_kv("Output", "%s", final_path);
            ui_kv("Size", "%s", ui_fmt_bytes(final_bytes));
            if (bitrate > 0 && avg_kbps > 0)
              ui_kv("Bitrate", "%.0f kbps avg  (%+.1f%% vs %d kbps target)", avg_kbps, delta_pct,
                    bitrate);
            ui_kv("Duration", "%s  encoded in %s  (%.2f× realtime)", ui_fmt_duration(info.duration),
                  ui_fmt_duration(elapsed), speed);
            ui_set_quiet(saved_quiet_done);
          }
        }
      } /* !scale_to_hd && !dry_run && !grain_only — end 4K encode block */

      /* ================================================================== */
      /* HD companion / scale-to-hd pass                                    */
      /* ================================================================== */
      if (do_hd) {
        /* Compute HD output dimensions from cropped source aspect ratio.
           Target width is always 1920; height is derived to preserve AR
           (rounded down to even for YUV420). */
        int hd_crop_w = info.width - (crop.error == 0 ? crop.left + crop.right : 0);
        int hd_crop_h = info.height - (crop.error == 0 ? crop.top + crop.bottom : 0);
        hd_crop_w = hd_crop_w & ~1;
        hd_crop_h = hd_crop_h & ~1;
        int hd_w = 1920;
        int hd_h = (int)((double)hd_crop_h * hd_w / hd_crop_w) & ~1;
        if (hd_h < 2)
          hd_h = 2;

        MediaInfo hd_info = info;
        hd_info.width = hd_w;
        hd_info.height = hd_h;

        /* DV Profile 8.1 is resolution-independent; keep it for HD. */
        HdrInfo hd_hdr = hdr;

        const EncodePreset *hd_preset = get_encode_preset(cli_quality, hd_h);
        int hd_vmaf_default = get_vmaf_target(cli_quality, hd_h);
        int hd_film_grain =
            get_film_grain_from_score(grain.error == 0 ? grain.grain_score : 0.0,
                                      grain.error == 0 ? grain.grain_variance : 0.0, cli_quality);

        /* HD output naming */
        char hd_output_name[1024] = "";
        char hd_base_name[1024] = "";
        if (saved_tmdb_title[0]) {
          build_output_filename(hd_output_name, sizeof(hd_output_name), saved_tmdb_title,
                                saved_tmdb_year, resolved_lang_tag, &hd_info, &hd_hdr, source);
          snprintf(hd_base_name, sizeof(hd_base_name), "%s", hd_output_name);
          char *hd_ext = strrchr(hd_base_name, '.');
          if (hd_ext && strcmp(hd_ext, ".mkv") == 0)
            *hd_ext = '\0';
        } else {
          /* blind mode: append -HDLight to input stem */
          snprintf(hd_base_name, sizeof(hd_base_name), "%s-HDLight", base_name);
          snprintf(hd_output_name, sizeof(hd_output_name), "%s.mkv", hd_base_name);
        }

        /* ---- HD CRF search ---- */
        int hd_crf = cli_crf;
        int hd_vmaf_used = 0;
        if (!grain_only && hd_crf == 0 && bitrate == 0) {
          hd_vmaf_used = cli_vmaf_target > 0 ? cli_vmaf_target : hd_vmaf_default;
          ui_section(companion_hd ? "HD CRF search" : "CRF search");
          CrfSearchResult hd_csr = run_crf_search(filepath, hd_vmaf_used, hd_preset, hd_film_grain,
                                                  "scale=1920:1080:flags=lanczos");
          if (hd_csr.ab_av1_missing) {
            ui_stage_fail("crf-search", "ab-av1 not found in PATH");
            ui_hint("install: brew install master-of-mint/tap/ab-av1  "
                    "or bypass with --crf <N> or --bitrate <kbps>");
            if (tracks.error == 0)
              free_media_tracks(&tracks);
            return 1;
          }
          if (hd_csr.crf < 0) {
            ui_stage_fail("crf-search", "ab-av1 exited with error or no CRF parsed");
            ui_hint("if params changed since last run, clear stale cache: "
                    "rm -rf ~/.cache/ab-av1/");
            if (tracks.error == 0)
              free_media_tracks(&tracks);
            return 1;
          }
          char hd_csr_detail[64];
          snprintf(hd_csr_detail, sizeof(hd_csr_detail), "CRF %d  (VMAF target %d)", hd_csr.crf,
                   hd_vmaf_used);
          ui_stage_ok("crf-search", hd_csr_detail);
          hd_crf = hd_csr.crf;
        } else if (cli_vmaf_target > 0) {
          hd_vmaf_used = cli_vmaf_target;
        }

        /* ---- HD plan section ---- */
        int hd_saved_quiet = ui_is_quiet();
        ui_set_quiet(0);
        ui_section(companion_hd ? "HD encoding plan" : "Encoding plan");
        ui_kv("Preset", "%s  (HD)", quality_type_to_string(cli_quality));
        ui_kv("SVT-AV1", "preset %d, tune %d, keyint %d, ac-bias %.1f", hd_preset->preset,
              hd_preset->tune, hd_preset->keyint, hd_preset->ac_bias);
        ui_kv("Scale", "%d×%d  (from %d×%d 4K source)", hd_w, hd_h, info.width, info.height);
        if (grain.error == 0) {
          int is_anim = (cli_quality == QUALITY_ANIMATION);
          const char *content_tier =
              is_anim ? "animation" : (grain.grain_score >= 0.08 ? "grainy" : "clean");
          ui_kv("Grain", "level %d  (%s tier)", hd_film_grain, content_tier);
        }
        if (hd_crf > 0) {
          if (hd_vmaf_used > 0)
            ui_kv("CRF", "%d  (VMAF target %d)", hd_crf, hd_vmaf_used);
          else
            ui_kv("CRF", "%d  (manual)", hd_crf);
        } else if (bitrate > 0) {
          ui_kv("Bitrate", "%d kbps VBR  (manual)", bitrate);
        } else {
          ui_kv("CRF", "(use --dry-run to probe, or --crf <N> to set)");
        }
        ui_kv("Output", "%s%s", output_dir, hd_output_name);

        if (grain_only)
          print_encoder_knobs(hd_preset, hd_film_grain);

        if (dry_run || grain_only) {
          ui_section(grain_only ? "Grain-only" : "Dry run");
          ui_row("No files written. Re-run without %s to encode.",
                 grain_only ? "--grain-only" : "--dry-run");
          if (tracks.error == 0)
            free_media_tracks(&tracks);
          return 0;
        }
        ui_set_quiet(hd_saved_quiet);

        /* For --scale-to-hd the 4K encode block was skipped, so RPU hasn't
           been extracted yet.  Do it here before the HD video encode. */
        if (scale_to_hd && hdr.error == 0 && hdr.has_dolby_vision) {
          char hd_rpu_name[2048];
          build_rpu_filename(hd_rpu_name, sizeof(hd_rpu_name), hd_base_name);

          /* Write RPU to cache directory */
          char hd_rpu_cache_path[4096];
          build_cache_path(hd_rpu_cache_path, sizeof(hd_rpu_cache_path), hd_rpu_name);
          snprintf(rpu_path, sizeof(rpu_path), "%s", hd_rpu_cache_path);

          ui_section("Dolby Vision RPU");
          time_t hd_rpu_t0 = time(NULL);
          RpuExtractResult hd_rpu_res = extract_rpu(filepath, rpu_path);

          if (hd_rpu_res.skipped) {
            ui_stage_skip(hd_rpu_name, "already exists");
          } else if (hd_rpu_res.error == 0) {
            char hd_rpu_detail[64];
            snprintf(hd_rpu_detail, sizeof(hd_rpu_detail), "%d RPUs in %s", hd_rpu_res.rpu_count,
                     ui_fmt_duration(difftime(time(NULL), hd_rpu_t0)));
            ui_stage_ok(hd_rpu_name, hd_rpu_detail);
          } else {
            char hd_rpu_err[64];
            snprintf(hd_rpu_err, sizeof(hd_rpu_err), "error %d", hd_rpu_res.error);
            ui_stage_fail(hd_rpu_name, hd_rpu_err);
            ui_hint("source claims Dolby Vision but RPU extraction failed; "
                    "encode will continue without DV metadata");
            rpu_path[0] = '\0';
          }
        }

        /* ---- HD video encode ---- */
        char hd_av1_video_name[2048];
        snprintf(hd_av1_video_name, sizeof(hd_av1_video_name), "%s.video.mkv", hd_base_name);
        char hd_av1_video_cache_path[4096];
        build_cache_path(hd_av1_video_cache_path, sizeof(hd_av1_video_cache_path),
                         hd_av1_video_name);
        char hd_av1_video_path[4096];
        snprintf(hd_av1_video_path, sizeof(hd_av1_video_path), "%s", hd_av1_video_cache_path);
        time_t hd_video_t0 = time(NULL);

        ui_section(companion_hd ? "HD video encoding" : "Video encoding");
        VideoEncodeConfig hd_vcfg = {
            .input_path = filepath,
            .output_path = hd_av1_video_path,
            .rpu_path = rpu_path[0] ? rpu_path : NULL,
            .preset = hd_preset,
            .film_grain = hd_film_grain,
            .grain_score = grain.error == 0 ? grain.grain_score : 0.0,
            .grain_variance = grain.error == 0 ? grain.grain_variance : 0.0,
            .target_bitrate = bitrate,
            .crf = hd_crf,
            .info = &info, /* source 4K dims for decoder */
            .crop = (crop.error == 0) ? &crop : NULL,
            .hdr = &hd_hdr,
            .scale_width = hd_w,
            .scale_height = hd_h,
        };
        VideoEncodeResult hd_vr = encode_video(&hd_vcfg);

        if (hd_vr.skipped) {
          ui_stage_skip("video.mkv", "already exists");
        } else if (hd_vr.error == 0) {
          char hd_vdetail[128];
          snprintf(hd_vdetail, sizeof(hd_vdetail), "%lld frames, %s in %s",
                   (long long)hd_vr.frames_encoded, ui_fmt_bytes(hd_vr.bytes_written),
                   ui_fmt_duration(difftime(time(NULL), hd_video_t0)));
          ui_stage_ok("video.mkv", hd_vdetail);
        } else {
          char hd_verr[64];
          snprintf(hd_verr, sizeof(hd_verr), "error %d after %lld frames", hd_vr.error,
                   (long long)hd_vr.frames_encoded);
          ui_stage_fail("video.mkv", hd_verr);
          ui_hint("re-run with --verbose to forward SVT-AV1's own log to "
                  "stderr");
        }

        /* ---- HD final mux (reuse opus + srt from this session) ---- */
        {
          char hd_final_path[4096];
          snprintf(hd_final_path, sizeof(hd_final_path), "%s%s", output_dir, hd_output_name);

          MuxAudioTrack hd_mux_audio[32];
          for (int i = 0; i < opus_count && i < 32; i++) {
            hd_mux_audio[i].path = opus_paths[i];
            hd_mux_audio[i].language = enc_best ? enc_best[i].language : "und";
            hd_mux_audio[i].track_name = audio_names[i];
            hd_mux_audio[i].is_default = (i == 0) ? 1 : 0;
          }

          MuxSubtitleTrack hd_mux_subs[64];
          int hd_sub_default_set = 0;
          for (int i = 0; i < srt_count && i < 64; i++) {
            hd_mux_subs[i].path = srt_paths[i];
            hd_mux_subs[i].language = srt_langs[i];
            hd_mux_subs[i].track_name = srt_names[i];
            hd_mux_subs[i].is_forced = srt_is_forced[i];
            hd_mux_subs[i].is_sdh = srt_is_sdh[i];
            if (!hd_sub_default_set && srt_is_forced[i] &&
                (strcmp(srt_langs[i], "fre") == 0 || strcmp(srt_langs[i], "fra") == 0)) {
              hd_mux_subs[i].is_default = 1;
              hd_sub_default_set = 1;
            } else {
              hd_mux_subs[i].is_default = 0;
            }
          }

          FinalMuxConfig hd_mux_cfg = {
              .video_path = hd_av1_video_path,
              .output_path = hd_final_path,
              .audio = hd_mux_audio,
              .audio_count = opus_count,
              .subs = hd_mux_subs,
              .sub_count = srt_count,
              .title = mkv_title,
              .video_title = mkv_title,
              .video_language = video_language,
              .chapters_source_path = filepath,
          };

          ui_section(companion_hd ? "HD final mux" : "Final mux");
          ui_kv("Inputs", "1 video + %d audio + %d subtitle track%s", opus_count, srt_count,
                srt_count == 1 ? "" : "s");
          time_t hd_mux_t0 = time(NULL);
          FinalMuxResult hd_mr = final_mux(&hd_mux_cfg);

          if (hd_mr.skipped) {
            ui_stage_skip(hd_output_name, "already exists");
          } else if (hd_mr.error == 0) {
            char hd_mux_detail[64];
            snprintf(hd_mux_detail, sizeof(hd_mux_detail), "%s",
                     ui_fmt_duration(difftime(time(NULL), hd_mux_t0)));
            ui_stage_ok(hd_output_name, hd_mux_detail);
          } else {
            char hd_mux_err[128];
            snprintf(hd_mux_err, sizeof(hd_mux_err), "error %d (%d audio + %d sub inputs)",
                     hd_mr.error, opus_count, srt_count);
            ui_stage_fail(hd_output_name, hd_mux_err);
            ui_hint("intermediates kept on disk; inspect them next to the "
                    "source file before re-running");
          }

          int hd_removed = 0;
          if (hd_mr.error == 0) {
            /* For scale-to-hd and companion-hd, remove shared audio/subtitle
               intermediates after HD mux. */
            if (scale_to_hd || companion_hd) {
              for (int i = 0; i < opus_count; i++)
                if (remove(opus_paths[i]) == 0)
                  hd_removed++;
              for (int i = 0; i < srt_count; i++)
                if (strncmp(srt_paths[i], output_dir, strlen(output_dir)) == 0)
                  if (remove(srt_paths[i]) == 0)
                    hd_removed++;
            }
            if (remove(hd_av1_video_path) == 0)
              hd_removed++;
            if (rpu_path[0] && remove(rpu_path) == 0)
              hd_removed++;
            if (hd_removed > 0) {
              char hd_clean_detail[64];
              snprintf(hd_clean_detail, sizeof(hd_clean_detail), "%d intermediate file%s",
                       hd_removed, hd_removed == 1 ? "" : "s");
              ui_stage_ok("Cleanup", hd_clean_detail);
            }
            /* Clean up cache directory when everything succeeded */
            if (hd_mr.error == 0)
              cleanup_cache_dir();
          }

          /* HD Done receipt */
          if (hd_mr.error == 0 && !hd_mr.skipped) {
            struct stat hd_fst;
            long long hd_final_bytes = 0;
            if (stat(hd_final_path, &hd_fst) == 0)
              hd_final_bytes = (long long)hd_fst.st_size;

            double hd_elapsed = difftime(time(NULL), encode_start_time);
            double hd_speed = hd_elapsed > 0.5 ? hd_info.duration / hd_elapsed : 0.0;

            int hd_done_quiet = ui_is_quiet();
            ui_set_quiet(0);
            ui_section(companion_hd ? "HD Done" : "Done");
            ui_kv("Output", "%s", hd_final_path);
            ui_kv("Size", "%s", ui_fmt_bytes(hd_final_bytes));
            ui_kv("Duration", "%s  encoded in %s  (%.2f× realtime)",
                  ui_fmt_duration(hd_info.duration), ui_fmt_duration(hd_elapsed), hd_speed);
            ui_set_quiet(hd_done_quiet);
          }
        }
      } /* do_hd */

      if (enc_best)
        free(enc_best);
    }
  }

  if (tracks.error == 0)
    free_media_tracks(&tracks);

  return 0;
}
