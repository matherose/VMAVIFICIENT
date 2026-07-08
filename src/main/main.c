/**
 * @file main.c
 * @brief Entry point for vmavificient.
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
#include "pipeline.h"
#include "proc.h"
#include "rpu_extract.h"
#include "subtitle_convert.h"
#include "tmdb.h"
#include "ui.h"
#include "utils.h"
#include "video_encode.h"

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

  switch (vmav_parse_int_or_zero(line)) {
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

  switch (vmav_parse_int_or_zero(line)) {
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

/**
 * @brief Prompt for a positive integer on stdin (fgets + parse, no scanf).
 * @return 0 on success, -1 on EOF/non-interactive stdin or 3 bad answers.
 */
static int ask_positive_int(const char *prompt, int *out) {
  char line[16];
  for (int tries = 0; tries < 3; tries++) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin))
      return -1;
    int v = vmav_parse_int_or_zero(line);
    if (v > 0) {
      *out = v;
      return 0;
    }
    printf("Please enter a positive number.\n");
  }
  return -1;
}

int main(int argc, char *argv[]) {
  init_logging();
  ui_init();
  PipelineCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    return 1;
  time_t encode_start_time = time(NULL);
  /* Set on any audio/video/mux failure; drives the process exit code. */
  int pipeline_failed = 0;
  printf("vmavificient v%s — SVT-AV1-HDR %s\n", VMAV_VERSION, get_svt_av1_version());

  int prescan_rc = vmav_cli_prescan(argc, argv, &ctx->opt);
  if (prescan_rc >= 0) {
    free(ctx);
    return prescan_rc;
  }
  if (!ctx->opt.blind)
    config_init();
  if (check_dependencies() != 0) {
    fprintf(stderr, "Fatal: dependency sanity check failed.\n");
    free(ctx);
    return 1;
  }
  int cli_rc = vmav_cli_parse(argc, argv, &ctx->opt);
  if (cli_rc >= 0) {
    free(ctx);
    return cli_rc;
  }
  int do_hd = ctx->opt.companion_hd || ctx->opt.scale_to_hd;

  /* ---- Cache directory setup ---- */
  if (strlen(ctx->opt.cache_dir) > 0) {
    /* User-provided cache directory */
    snprintf(ctx->cache_dir, sizeof(ctx->cache_dir), "%s", ctx->opt.cache_dir);
  } else {
    /* Default: .vmavificient-cache in project root */
    snprintf(ctx->cache_dir, sizeof(ctx->cache_dir), "./.vmavificient-cache");
  }
  if (mkdir(ctx->cache_dir, 0755) != 0 && errno != EEXIST) {
    char err[128];
    snprintf(err, sizeof(err), "failed to create cache directory '%s' (errno %d)", ctx->cache_dir,
             errno);
    ui_stage_fail("Cache", err);
    free(ctx);
    return 1;
  }

  const char *filepath = ctx->opt.filepath;

  /* Cache scores file path */
  snprintf(ctx->scores_cache_path, sizeof(ctx->scores_cache_path), "%s/scores.json",
           ctx->cache_dir);

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
    free(ctx);
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

    int split_fre = (ctx->opt.lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;
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
            vmav_codec_short(tracks.subtitles[i].codec), type,
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

  snprintf(video_4k_cache_path, sizeof(video_4k_cache_path), "%s/%s.video.mkv", ctx->cache_dir,
           base_for_cache);
  if (do_hd && !ctx->opt.scale_to_hd) {
    snprintf(video_hd_cache_path, sizeof(video_hd_cache_path), "%s/%s-HDLight.video.mkv",
             ctx->cache_dir, base_for_cache);
  } else {
    snprintf(video_hd_cache_path, sizeof(video_hd_cache_path), "%s/%s.video.mkv", ctx->cache_dir,
             base_for_cache);
  }

  if (vmav_file_exists(video_4k_cache_path)) {
    ui_section("Resume check");
    ui_stage_ok("skip", "4K video.mkv already present in cache, proceeding to mux");
    free(ctx);
    return 0;
  }

  if (vmav_file_exists(video_hd_cache_path)) {
    ui_section("Resume check");
    ui_stage_ok("skip", "HD video.mkv already present in cache, proceeding to mux");
    free(ctx);
    return 0;
  }

  /* ---- OCR preflight: verify tessdata before any expensive work ---- */
  if (tracks.error == 0 && tracks.subtitle_count > 0 && !ctx->opt.dry_run && !ctx->opt.grain_only) {
    for (int i = 0; i < tracks.subtitle_count; i++) {
      TrackInfo *sub = &tracks.subtitles[i];
      if (sub->is_karaoke || !is_pgs_subtitle(sub))
        continue;
      const char *lang = sub->language[0] ? sub->language : "und";
      const char *tess_lang = iso639_to_tesseract_lang(lang);
      if (subtitle_ocr_preflight(tess_lang, NULL, 0) != 0) {
        ui_stage_fail("OCR preflight", "no usable tessdata for PGS subtitle OCR");
        ui_hint("install tessdata_best (eng+fra) and set TESSDATA_PREFIX, or drop PGS tracks");
        free(ctx);
        return 1;
      }
    }
  }

  /* ---- Grain analysis ---- */
  ui_section("Grain analysis");
  int film_grain = 0;

  /* Check for cached scores (uses global cached_* variables) */
  scores_cached = vmav_load_cached_scores(ctx->scores_cache_path, &cached_grain_score,
                                          &cached_grain_variance, &cached_crf);

  if (scores_cached) {
    film_grain =
        get_film_grain_from_score(cached_grain_score, cached_grain_variance, ctx->opt.quality);
    ui_kv("Luma score", "%.4f  (from cache)", cached_grain_score);
    ui_kv("Chroma score", "%.4f", cached_grain_variance * 100); /* approximate */
    ui_kv("Variance", "%.4f  (from cache)", cached_grain_variance);
    ui_kv("Synth level", "%d  (from cache)", film_grain);
    ui_stage_ok("Grain analysis", "loaded from cache");
    /* Refresh timestamp when using cached scores */
    if (!ctx->opt.crf && !ctx->opt.bitrate) {
      vmav_save_cached_scores(ctx->scores_cache_path, cached_grain_score, cached_grain_variance,
                              cached_crf);
    }
  } else {
    ui_row("Sampling 4 windows (extends to 7 if variance is high)…");
    grain = get_grain_score(filepath);
    if (grain.error == 0) {
      film_grain =
          get_film_grain_from_score(grain.grain_score, grain.grain_variance, ctx->opt.quality);
      /* Per-window OK/FAIL lines already printed by media_analysis as it
         worked; these summary kvs collect the aggregate signal. */
      ui_kv("Luma score", "%.4f  (max across windows)", grain.grain_score);
      ui_kv("Chroma score", "%.4f", grain.grain_score * 100); /* approximate */
      ui_kv("Variance", "%.4f%s", grain.grain_variance,
            grain.windows_succeeded > 4 ? "  (refinement triggered)" : "");
      ui_kv("Synth level", "%d  (0–50)", film_grain);
      /* Save grain scores to cache */
      if (!ctx->opt.crf && !ctx->opt.bitrate) {
        if (!vmav_save_cached_scores(ctx->scores_cache_path, grain.grain_score,
                                     grain.grain_variance, 0)) {
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

  const EncodePreset *enc_preset = get_encode_preset(ctx->opt.quality, info.height);

  if (do_hd && info.height < 2160) {
    ui_stage_fail("Source", "--companion-hd / --scale-to-hd requires a 4K source "
                            "(height >= 2160)");
    free(ctx);
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
  EpisodeInfo saved_episode = {0};
  bool saved_is_tv = false;
  const char *video_language = "und";
  SourceType source = ctx->opt.source;
  FrenchVariant fv = FRENCH_VARIANT_UNKNOWN;
  FrenchAudioOrigin fr_audio_origin = FRENCH_AUDIO_VFF;
  LanguageTag resolved_lang_tag = ctx->opt.lang_tag;
  bool naming_ok = false;

  if (ctx->opt.blind) {
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
  } else if (ctx->opt.tmdb_id > 0) {
    /* Common metadata, filled from the movie or TV endpoint. */
    char meta_title[512] = "";
    int meta_year = 0;
    char meta_lang[8] = "";
    bool meta_ok = false;
    EpisodeInfo ep = {0};

    if (ctx->opt.tv_mode) {
      /* S/E resolution: flags > filename parse > interactive prompt. */
      int season = ctx->opt.season;
      int episode = ctx->opt.episode;
      if (season <= 0 || episode <= 0) {
        const char *fname = strrchr(filepath, '/');
        fname = fname ? fname + 1 : filepath;
        int ps = 0, pe = 0;
        if (parse_season_episode(fname, &ps, &pe) == 0) {
          if (season <= 0)
            season = ps;
          if (episode <= 0)
            episode = pe;
        }
      }
      if (season <= 0 &&
          ask_positive_int("\nSeason not detected from filename. Season: ", &season) != 0) {
        ui_stage_fail("Naming", "season unknown; pass --season <N>");
        free(ctx);
        return 1;
      }
      if (episode <= 0 &&
          ask_positive_int("Episode not detected from filename. Episode: ", &episode) != 0) {
        ui_stage_fail("Naming", "episode unknown; pass --episode <N>");
        free(ctx);
        return 1;
      }

      ui_section("TMDB lookup");
      ui_kv("TV ID", "%d", ctx->opt.tmdb_id);
      ui_kv("Episode", "S%02dE%02d", season, episode);
      TmdbTvInfo tv = tmdb_fetch_tv(ctx->opt.tmdb_id);
      if (tv.error != 0) {
        ui_stage_fail("TMDB fetch", "could not fetch TV show info");
        ui_hint("verify TMDB_API_KEY is set in config.ini and the ID is a "
                "TV series ID (tmdb.org/tv/<id>, not /movie/)");
      } else {
        snprintf(meta_title, sizeof(meta_title), "%s", tv.original_name);
        meta_year = tv.first_air_year;
        snprintf(meta_lang, sizeof(meta_lang), "%s", tv.original_language);
        ep.season = season;
        ep.episode = episode;
        TmdbEpisodeInfo epi = tmdb_fetch_episode(ctx->opt.tmdb_id, season, episode);
        if (epi.error == 0)
          snprintf(ep.title, sizeof(ep.title), "%s", epi.name);
        else
          ui_hint("episode title unavailable on TMDB; filename will omit it");
        meta_ok = true;
      }
    } else {
      ui_section("TMDB lookup");
      ui_kv("Movie ID", "%d", ctx->opt.tmdb_id);
      TmdbMovieInfo tmdb = tmdb_fetch_movie(ctx->opt.tmdb_id);
      if (tmdb.error != 0) {
        ui_stage_fail("TMDB fetch", "could not fetch movie info");
        ui_hint("verify TMDB_API_KEY is set in config.ini and the ID is "
                "correct (e.g. tmdb.org/movie/<id>)");
      } else {
        snprintf(meta_title, sizeof(meta_title), "%s", tmdb.original_title);
        meta_year = tmdb.release_year;
        snprintf(meta_lang, sizeof(meta_lang), "%s", tmdb.original_language);
        meta_ok = true;
      }
    }

    if (meta_ok) {
      ui_kv("Title", "%s", meta_title);
      if (ctx->opt.tv_mode) {
        if (ep.title[0])
          ui_kv("Episode title", "%s", ep.title);
      } else {
        ui_kv("Year", "%d", meta_year);
      }
      ui_kv("Language", "%s", meta_lang);

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
      if (ctx->opt.lang_tag != LANG_TAG_NONE) {
        lang_tag = ctx->opt.lang_tag;
      } else {
        LanguageTag auto_tag = determine_language_tag(&tracks, meta_lang, fv);

        /* If auto-detection produced a definitive result, use it.
           Otherwise ask the user interactively. */
        if (auto_tag != LANG_TAG_VO || tracks.audio_count <= 1) {
          lang_tag = auto_tag;
        } else {
          lang_tag = ask_language_tag(&tracks);
        }
      }
      resolved_lang_tag = lang_tag;

      snprintf(saved_tmdb_title, sizeof(saved_tmdb_title), "%s", meta_title);
      saved_tmdb_year = meta_year;
      saved_episode = ep;
      saved_is_tv = ctx->opt.tv_mode;

      build_output_filename(output_name, sizeof(output_name), meta_title, meta_year, lang_tag,
                            &info, &hdr, source, ctx->opt.tv_mode ? &ep : NULL);

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

      if (strcmp(meta_lang, "fr") == 0) {
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

      if (ctx->opt.tv_mode) {
        if (ep.title[0])
          snprintf(mkv_title, sizeof(mkv_title), "%s - S%02dE%02d - %s", meta_title, ep.season,
                   ep.episode, ep.title);
        else
          snprintf(mkv_title, sizeof(mkv_title), "%s - S%02dE%02d", meta_title, ep.season,
                   ep.episode);
      } else {
        snprintf(mkv_title, sizeof(mkv_title), "%s (%d)", meta_title, meta_year);
      }
      {
        const char *vlang = iso639_1_to_2b(meta_lang);
        if (vlang)
          video_language = vlang;
      }

      naming_ok = true;
    }
  } else {
    /* Neither --blind nor --tmdb: without this branch the pipeline body
       below is silently skipped and the program exits 0 after grain
       analysis, having encoded nothing. */
    ui_stage_fail("Naming", "no naming source: pass --tmdb <id> or --blind");
    ui_hint("--tmdb <id> names from TMDB metadata; --blind names the "
            "output <input-stem>.mkv next to the source");
    if (tracks.error == 0)
      free_media_tracks(&tracks);
    free(ctx);
    return 1;
  }

  if (naming_ok) {
    /* ---- Rate control: CRF search or manual override ---- */
    int crf = ctx->opt.crf;         /* 0 if not specified */
    int bitrate = ctx->opt.bitrate; /* 0 if not specified; VBR only when set */
    int vmaf_used = 0;

    if (!ctx->opt.scale_to_hd) {
      /* ---- 4K rate control + plan ---- */
      if (!ctx->opt.grain_only && crf == 0 && bitrate == 0) {
        /* Check for cached CRF before running search */
        if (scores_cached && cached_crf > 0) {
          crf = cached_crf;
          vmaf_used = ctx->opt.vmaf_target > 0 ? ctx->opt.vmaf_target
                                               : get_vmaf_target(ctx->opt.quality, info.height);
          ui_section("CRF search");
          char detail[64];
          snprintf(detail, sizeof(detail), "using cached CRF %d", crf);
          ui_stage_ok("skip", detail);
        } else {
          /* Default path: probe CRF in-process at source (4K) resolution. */
          vmaf_used = ctx->opt.vmaf_target > 0 ? ctx->opt.vmaf_target
                                               : get_vmaf_target(ctx->opt.quality, info.height);
          ui_section("CRF search");
          CrfSearchResult csr = run_crf_search(filepath, vmaf_used, enc_preset, film_grain, NULL);
          if (csr.crf < 0) {
            ui_stage_fail("crf-search", csr.error ? csr.error : "CRF search failed");
            ui_hint("bypass with --crf <N> or --bitrate <kbps>");
            if (tracks.error == 0)
              free_media_tracks(&tracks);
            free(ctx);
            return 1;
          }
          char detail[96];
          snprintf(detail, sizeof(detail), "CRF %d  (VMAF %.2f, target %d)", csr.crf,
                   csr.vmaf_result, vmaf_used);
          ui_stage_ok("crf-search", detail);
          crf = csr.crf;
        }
      } else if (ctx->opt.vmaf_target > 0) {
        vmaf_used = ctx->opt.vmaf_target;
      }

      /* Plan + Dry-run notice always render. */
      int saved_quiet = ui_is_quiet();
      ui_set_quiet(0);
      ui_section("Encoding plan");
      ui_kv("Preset", "%s  (%s)", quality_type_to_string(ctx->opt.quality),
            info.height >= 2160 ? "4K" : "HD");
      ui_kv("SVT-AV1", "preset %d, tune %d, keyint %d, ac-bias %.1f", enc_preset->preset,
            enc_preset->tune, enc_preset->keyint, enc_preset->ac_bias);
      if (grain.error == 0) {
        int is_anim = (ctx->opt.quality == QUALITY_ANIMATION);
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

      if (ctx->opt.grain_only)
        vmav_print_encoder_knobs(enc_preset, film_grain);

      /* For companion-hd, fall through so the HD plan section also renders
         before exiting.  For solo dry-run / grain-only, exit here. */
      if ((ctx->opt.dry_run || ctx->opt.grain_only) && !ctx->opt.companion_hd) {
        ui_section(ctx->opt.grain_only ? "Grain-only" : "Dry run");
        ui_row("No files written. Re-run without %s to encode.",
               ctx->opt.grain_only ? "--grain-only" : "--dry-run");
        if (tracks.error == 0)
          free_media_tracks(&tracks);
        free(ctx);
        return 0;
      }
      ui_set_quiet(saved_quiet);
    } /* !ctx->opt.scale_to_hd */

    {
      /* ---- OPUS audio encoding ---- */
      int enc_best_count = 0;
      int enc_split_fre = (resolved_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;
      TrackInfo *enc_best = select_best_audio_per_language(&tracks, enc_split_fre, &enc_best_count);

      /* Sort: French first, then English, then others */
      if (enc_best && enc_best_count > 1)
        qsort(enc_best, enc_best_count, sizeof(TrackInfo), vmav_cmp_audio_order);

      /* Store OPUS paths, track names and languages for final mux.
         All three are written through the opus_count write-index so a
         failed track never leaves a hole in the mux inputs. */
      char opus_paths[32][4096];
      char audio_names[32][256];
      char audio_langs[32][16];
      int opus_count = 0;
      int audio_fail_count = 0;

      if (enc_best && enc_best_count > 0 && !ctx->opt.dry_run && !ctx->opt.grain_only) {
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
          vmav_build_cache_path(ctx, opus_cache_path, sizeof(opus_cache_path), opus_name);
          snprintf(opus_paths[opus_count], sizeof(opus_paths[0]), "%s", opus_cache_path);

          /* Build display name and language for MKV track */
          build_audio_track_name(audio_names[opus_count], sizeof(audio_names[0]),
                                 enc_best[i].language, enc_best[i].channels, track_origin);
          snprintf(audio_langs[opus_count], sizeof(audio_langs[0]), "%s", enc_best[i].language);

          ui_row("[%d/%d] %s  %s  %dch  %lld kbps  →  \"%s\"", i + 1, enc_best_count,
                 enc_best[i].language, enc_best[i].codec, enc_best[i].channels,
                 (long long)(enc_best[i].bitrate / 1000), audio_names[opus_count]);

          time_t track_t0 = time(NULL);
          OpusEncodeResult r = encode_track_to_opus(filepath, &enc_best[i], opus_paths[opus_count]);

          if (r.skipped) {
            ui_stage_skip(opus_name, "already exists");
            opus_count++;
          } else if (r.error == 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%s", ui_fmt_duration(difftime(time(NULL), track_t0)));
            ui_stage_ok(opus_name, detail);
            opus_count++;
          } else {
            char err[128];
            snprintf(err, sizeof(err), "stream #%d (%s, %dch): error %d", enc_best[i].index,
                     enc_best[i].codec, enc_best[i].channels, r.error);
            ui_stage_fail(opus_name, err);
            ui_hint("verify the source stream is decodable; opusenc-style "
                    "channel layouts (>2ch) require ffmpeg with libopus");
            audio_fail_count++;
            pipeline_failed = 1;
          }
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

      if (tracks.error == 0 && tracks.subtitle_count > 0 && !ctx->opt.dry_run &&
          !ctx->opt.grain_only) {
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
            vmav_build_cache_path(ctx, srt_cache_path, sizeof(srt_cache_path), srt_fname);
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
              /* If already SRT (subrip), copy stream; else convert to srt. */
              const char *codec_arg = (strcmp(sub->codec, "subrip") == 0) ? "copy" : "srt";
              VmavCommand c;
              vmav_cmd_init(&c);
              vmav_cmd_arg(&c, "ffmpeg");
              vmav_cmd_arg(&c, "-y");
              vmav_cmd_arg(&c, "-loglevel");
              vmav_cmd_arg(&c, "error");
              vmav_cmd_arg(&c, "-i");
              vmav_cmd_arg(&c, filepath);
              vmav_cmd_arg(&c, "-map");
              vmav_cmd_argf(&c, "0:%d", sub->index);
              vmav_cmd_arg(&c, "-c:s");
              vmav_cmd_arg(&c, codec_arg);
              vmav_cmd_arg(&c, srt_paths[srt_count]);

              ui_row("Extract  #%-2d  %s  %s  →  \"%s\"", sub->index, lang, sub->codec,
                     srt_names[srt_count]);

              int exit_code = vmav_run(c.argv);
              struct stat srt_out_st;
              if (exit_code == 0 && stat(srt_paths[srt_count], &srt_out_st) == 0 &&
                  srt_out_st.st_size > 0) {
                ui_stage_ok(srt_fname, NULL);
                srt_count++;
              } else if (exit_code == 0) {
                /* ffmpeg succeeded but wrote no events (e.g. a forced track
                   with nothing to force in this cut). An empty SRT is not a
                   valid mux input, so drop it. */
                remove(srt_paths[srt_count]);
                ui_stage_skip(srt_fname, "no subtitle events in stream");
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
            vmav_build_cache_path(ctx, srt_cache_path, sizeof(srt_cache_path), srt_fname);
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
      for (int i = 0; i < ctx->opt.extra_srt_count && srt_count < 64; i++) {
        snprintf(srt_paths[srt_count], sizeof(srt_paths[0]), "%s", ctx->opt.extra_srt_paths[i]);

        /* Try to guess language from filename */
        const char *srt_lang = "und";
        if (strstr(ctx->opt.extra_srt_paths[i], ".fre.") ||
            strstr(ctx->opt.extra_srt_paths[i], ".fra."))
          srt_lang = "fre";
        else if (strstr(ctx->opt.extra_srt_paths[i], ".eng."))
          srt_lang = "eng";
        else if (strstr(ctx->opt.extra_srt_paths[i], ".ger.") ||
                 strstr(ctx->opt.extra_srt_paths[i], ".deu."))
          srt_lang = "ger";
        else if (strstr(ctx->opt.extra_srt_paths[i], ".spa."))
          srt_lang = "spa";
        else if (strstr(ctx->opt.extra_srt_paths[i], ".ita."))
          srt_lang = "ita";

        int forced = (strstr(ctx->opt.extra_srt_paths[i], "forced") != NULL) ? 1 : 0;
        int sdh = (strstr(ctx->opt.extra_srt_paths[i], "sdh") != NULL ||
                   strstr(ctx->opt.extra_srt_paths[i], "SDH") != NULL)
                      ? 1
                      : 0;

        build_subtitle_track_name(srt_names[srt_count], sizeof(srt_names[0]), srt_lang, 1, forced,
                                  sdh, fr_audio_origin);
        snprintf(srt_langs[srt_count], sizeof(srt_langs[0]), "%s", srt_lang);
        srt_is_forced[srt_count] = forced;
        srt_is_sdh[srt_count] = sdh;
        srt_variant[srt_count] = 0;

        printf("  [SRT]  %s → \"%s\"\n", ctx->opt.extra_srt_paths[i], srt_names[srt_count]);
        srt_count++;
      }

      /* ---- Sort subtitles: French Forced → French → English → Others ---- */
      if (srt_count > 1) {
        int order_idx[64];
        int order_key[64];
        for (int i = 0; i < srt_count; i++) {
          order_idx[i] = i;
          order_key[i] = vmav_sub_sort_key(srt_langs[i], srt_is_forced[i]);
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
      if (!ctx->opt.scale_to_hd && !ctx->opt.dry_run &&
          !ctx->opt.grain_only) { /* 4K encode block */
        if (hdr.error == 0 && hdr.has_dolby_vision) {
          char rpu_name[2048];
          build_rpu_filename(rpu_name, sizeof(rpu_name), base_name);

          /* Write RPU to cache directory */
          char rpu_cache_path[4096];
          vmav_build_cache_path(ctx, rpu_cache_path, sizeof(rpu_cache_path), rpu_name);
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
        vmav_build_cache_path(ctx, av1_video_cache_path, sizeof(av1_video_cache_path),
                              av1_video_name);
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
            pipeline_failed = 1;
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
            mux_audio[i].language = audio_langs[i];
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
          FinalMuxResult mr = {.error = -1, .skipped = 0};

          if (audio_fail_count > 0) {
            char err[128];
            snprintf(err, sizeof(err), "%d audio track%s failed to encode — mux skipped",
                     audio_fail_count, audio_fail_count == 1 ? "" : "s");
            ui_stage_fail(output_name, err);
            ui_hint("a release missing an announced audio track is broken; "
                    "cached intermediates are kept, fix the source and re-run");
            pipeline_failed = 1;
          } else if (vr.error != 0) {
            ui_stage_fail(output_name, "video encode failed — mux skipped");
            pipeline_failed = 1;
          } else {
            mr = final_mux(&mux_cfg);

            if (mr.skipped) {
              ui_stage_skip(output_name, "already exists");
            } else if (mr.error == 0) {
              char detail[64];
              snprintf(detail, sizeof(detail), "%s", ui_fmt_duration(difftime(time(NULL), mux_t0)));
              ui_stage_ok(output_name, detail);
            } else {
              char err[128];
              snprintf(err, sizeof(err), "error %d (%d audio + %d sub inputs)", mr.error,
                       opus_count, srt_count);
              ui_stage_fail(output_name, err);
              ui_hint("intermediates kept on disk; inspect them next to the "
                      "source file before re-running");
              pipeline_failed = 1;
            }
          }

          /* Clean up intermediate files on success. Leaving sidecar .srt
             files next to the final MKV causes players to auto-load them
             as external subtitles, overriding the embedded defaults. */
          int removed = 0;
          if (mr.error == 0) {
            /* For --companion-hd and --scale-to-hd, audio/subtitle intermediates
               are reused by the HD mux; defer their removal to the HD cleanup pass.
               Only the 4K video.mkv and RPU (if not DV) are removed here. */
            if (!ctx->opt.companion_hd) {
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
            if (rpu_path[0] && !ctx->opt.companion_hd && remove(rpu_path) == 0)
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
            if (mr.error == 0 && !ctx->opt.companion_hd)
              vmav_cleanup_cache_dir(ctx);
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
      } /* !ctx->opt.scale_to_hd && !ctx->opt.dry_run && !ctx->opt.grain_only — end 4K encode block
         */

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

        const EncodePreset *hd_preset = get_encode_preset(ctx->opt.quality, hd_h);
        int hd_vmaf_default = get_vmaf_target(ctx->opt.quality, hd_h);
        int hd_film_grain = get_film_grain_from_score(grain.error == 0 ? grain.grain_score : 0.0,
                                                      grain.error == 0 ? grain.grain_variance : 0.0,
                                                      ctx->opt.quality);

        /* HD output naming */
        char hd_output_name[1024] = "";
        char hd_base_name[1024] = "";
        if (saved_tmdb_title[0]) {
          build_output_filename(hd_output_name, sizeof(hd_output_name), saved_tmdb_title,
                                saved_tmdb_year, resolved_lang_tag, &hd_info, &hd_hdr, source,
                                saved_is_tv ? &saved_episode : NULL);
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
        int hd_crf = ctx->opt.crf;
        int hd_vmaf_used = 0;
        if (!ctx->opt.grain_only && hd_crf == 0 && bitrate == 0) {
          hd_vmaf_used = ctx->opt.vmaf_target > 0 ? ctx->opt.vmaf_target : hd_vmaf_default;
          ui_section(ctx->opt.companion_hd ? "HD CRF search" : "CRF search");
          CrfSearchResult hd_csr = run_crf_search(filepath, hd_vmaf_used, hd_preset, hd_film_grain,
                                                  "scale=1920:1080:flags=lanczos");
          if (hd_csr.crf < 0) {
            ui_stage_fail("crf-search", hd_csr.error ? hd_csr.error : "CRF search failed");
            ui_hint("bypass with --crf <N> or --bitrate <kbps>");
            if (tracks.error == 0)
              free_media_tracks(&tracks);
            free(ctx);
            return 1;
          }
          char hd_csr_detail[96];
          snprintf(hd_csr_detail, sizeof(hd_csr_detail), "CRF %d  (VMAF %.2f, target %d)",
                   hd_csr.crf, hd_csr.vmaf_result, hd_vmaf_used);
          ui_stage_ok("crf-search", hd_csr_detail);
          hd_crf = hd_csr.crf;
        } else if (ctx->opt.vmaf_target > 0) {
          hd_vmaf_used = ctx->opt.vmaf_target;
        }

        /* ---- HD plan section ---- */
        int hd_saved_quiet = ui_is_quiet();
        ui_set_quiet(0);
        ui_section(ctx->opt.companion_hd ? "HD encoding plan" : "Encoding plan");
        ui_kv("Preset", "%s  (HD)", quality_type_to_string(ctx->opt.quality));
        ui_kv("SVT-AV1", "preset %d, tune %d, keyint %d, ac-bias %.1f", hd_preset->preset,
              hd_preset->tune, hd_preset->keyint, hd_preset->ac_bias);
        ui_kv("Scale", "%d×%d  (from %d×%d 4K source)", hd_w, hd_h, info.width, info.height);
        if (grain.error == 0) {
          int is_anim = (ctx->opt.quality == QUALITY_ANIMATION);
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

        if (ctx->opt.grain_only)
          vmav_print_encoder_knobs(hd_preset, hd_film_grain);

        if (ctx->opt.dry_run || ctx->opt.grain_only) {
          ui_section(ctx->opt.grain_only ? "Grain-only" : "Dry run");
          ui_row("No files written. Re-run without %s to encode.",
                 ctx->opt.grain_only ? "--grain-only" : "--dry-run");
          if (tracks.error == 0)
            free_media_tracks(&tracks);
          free(ctx);
          return 0;
        }
        ui_set_quiet(hd_saved_quiet);

        /* For --scale-to-hd the 4K encode block was skipped, so RPU hasn't
           been extracted yet.  Do it here before the HD video encode. */
        if (ctx->opt.scale_to_hd && hdr.error == 0 && hdr.has_dolby_vision) {
          char hd_rpu_name[2048];
          build_rpu_filename(hd_rpu_name, sizeof(hd_rpu_name), hd_base_name);

          /* Write RPU to cache directory */
          char hd_rpu_cache_path[4096];
          vmav_build_cache_path(ctx, hd_rpu_cache_path, sizeof(hd_rpu_cache_path), hd_rpu_name);
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
        vmav_build_cache_path(ctx, hd_av1_video_cache_path, sizeof(hd_av1_video_cache_path),
                              hd_av1_video_name);
        char hd_av1_video_path[4096];
        snprintf(hd_av1_video_path, sizeof(hd_av1_video_path), "%s", hd_av1_video_cache_path);
        time_t hd_video_t0 = time(NULL);

        ui_section(ctx->opt.companion_hd ? "HD video encoding" : "Video encoding");
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
          pipeline_failed = 1;
        }

        /* ---- HD final mux (reuse opus + srt from this session) ---- */
        {
          char hd_final_path[4096];
          snprintf(hd_final_path, sizeof(hd_final_path), "%s%s", output_dir, hd_output_name);

          MuxAudioTrack hd_mux_audio[32];
          for (int i = 0; i < opus_count && i < 32; i++) {
            hd_mux_audio[i].path = opus_paths[i];
            hd_mux_audio[i].language = audio_langs[i];
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

          ui_section(ctx->opt.companion_hd ? "HD final mux" : "Final mux");
          ui_kv("Inputs", "1 video + %d audio + %d subtitle track%s", opus_count, srt_count,
                srt_count == 1 ? "" : "s");
          time_t hd_mux_t0 = time(NULL);
          FinalMuxResult hd_mr = {.error = -1, .skipped = 0};

          if (audio_fail_count > 0) {
            char err[128];
            snprintf(err, sizeof(err), "%d audio track%s failed to encode — mux skipped",
                     audio_fail_count, audio_fail_count == 1 ? "" : "s");
            ui_stage_fail(hd_output_name, err);
            ui_hint("a release missing an announced audio track is broken; "
                    "cached intermediates are kept, fix the source and re-run");
            pipeline_failed = 1;
          } else if (hd_vr.error != 0) {
            ui_stage_fail(hd_output_name, "video encode failed — mux skipped");
            pipeline_failed = 1;
          } else {
            hd_mr = final_mux(&hd_mux_cfg);

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
              pipeline_failed = 1;
            }
          }

          int hd_removed = 0;
          if (hd_mr.error == 0) {
            /* For scale-to-hd and companion-hd, remove shared audio/subtitle
               intermediates after HD mux. */
            if (ctx->opt.scale_to_hd || ctx->opt.companion_hd) {
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
              vmav_cleanup_cache_dir(ctx);
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
            ui_section(ctx->opt.companion_hd ? "HD Done" : "Done");
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

  free(ctx);
  return pipeline_failed ? 1 : 0;
}
