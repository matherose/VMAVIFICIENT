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

int main(int argc, char *argv[]) {
  init_logging();
  ui_init();
  PipelineCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    return 1;
  ctx->encode_start_time = time(NULL);
  int rc = 0;
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

  /* Cache scores file path */
  snprintf(ctx->scores_cache_path, sizeof(ctx->scores_cache_path), "%s/scores.json",
           ctx->cache_dir);

  StageStatus st = stage_probe(ctx);
  if (st != STAGE_CONTINUE) {
    rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
    goto out;
  }

  st = stage_grain(ctx);
  if (st != STAGE_CONTINUE) {
    rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
    goto out;
  }

  st = stage_naming(ctx);
  if (st != STAGE_CONTINUE) {
    rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
    goto out;
  }

  EncodePassParams pass4k = {
      .preset = ctx->enc_preset,
      .film_grain = ctx->film_grain,
      .crf = ctx->opt.crf,
      .base_name = ctx->base_name,
      .output_name = ctx->output_name,
      .crf_scale_filter = NULL,
      .plan_info = &ctx->info,
      .enc_hdr = &ctx->hdr,
      .is_hd = false,
      .use_cached_crf = true,
      .dry_run_falls_through = ctx->opt.companion_hd,
      .extract_rpu = true,
      .defer_shared_cleanup = ctx->opt.companion_hd,
  };
  if (!ctx->opt.scale_to_hd) {
    st = stage_rate_plan(ctx, &pass4k);
    if (st != STAGE_CONTINUE) {
      rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
      goto out;
    }
  }

  {
    st = stage_audio(ctx);
    if (st != STAGE_CONTINUE) {
      rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
      goto out;
    }

    st = stage_subs(ctx);
    if (st != STAGE_CONTINUE) {
      rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
      goto out;
    }

    if (!ctx->opt.scale_to_hd && !ctx->opt.dry_run && !ctx->opt.grain_only) {
      st = encode_pass(ctx, &pass4k);
      if (st != STAGE_CONTINUE) {
        rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
        goto out;
      }
    }

    /* ================================================================== */
    /* HD companion / scale-to-hd pass                                    */
    /* ================================================================== */
    if (do_hd) {
      /* HD output dimensions from the cropped source AR (width 1920,
         height even, min 2) — moved verbatim from the old HD block. */
      int hd_crop_w =
          ctx->info.width - (ctx->crop.error == 0 ? ctx->crop.left + ctx->crop.right : 0);
      int hd_crop_h =
          ctx->info.height - (ctx->crop.error == 0 ? ctx->crop.top + ctx->crop.bottom : 0);
      hd_crop_w &= ~1;
      hd_crop_h &= ~1;
      int hd_w = 1920;
      int hd_h = (int)((double)hd_crop_h * hd_w / hd_crop_w) & ~1;
      if (hd_h < 2)
        hd_h = 2;

      MediaInfo hd_info = ctx->info;
      hd_info.width = hd_w;
      hd_info.height = hd_h;
      HdrInfo hd_hdr = ctx->hdr; /* DV Profile 8.1 is resolution-independent */

      char hd_output_name[1024] = "";
      char hd_base_name[1024] = "";
      if (ctx->saved_tmdb_title[0]) {
        build_output_filename(hd_output_name, sizeof(hd_output_name), ctx->saved_tmdb_title,
                              ctx->saved_tmdb_year, ctx->resolved_lang_tag, &hd_info, &hd_hdr,
                              ctx->source, ctx->saved_is_tv ? &ctx->saved_episode : NULL);
        snprintf(hd_base_name, sizeof(hd_base_name), "%s", hd_output_name);
        char *hd_ext = strrchr(hd_base_name, '.');
        if (hd_ext && strcmp(hd_ext, ".mkv") == 0)
          *hd_ext = '\0';
      } else {
        snprintf(hd_base_name, sizeof(hd_base_name), "%s-HDLight", ctx->base_name);
        snprintf(hd_output_name, sizeof(hd_output_name), "%s.mkv", hd_base_name);
      }

      EncodePassParams passhd = {
          .preset = get_encode_preset(ctx->opt.quality, hd_h),
          .film_grain = get_film_grain_from_score(
              ctx->grain.error == 0 ? ctx->grain.grain_score : 0.0,
              ctx->grain.error == 0 ? ctx->grain.grain_variance : 0.0, ctx->opt.quality),
          .crf = ctx->opt.crf,
          .base_name = hd_base_name,
          .output_name = hd_output_name,
          .scale_width = hd_w,
          .scale_height = hd_h,
          .crf_scale_filter = "scale=1920:1080:flags=lanczos",
          .plan_info = &hd_info,
          .enc_hdr = &hd_hdr,
          .is_hd = true,
          .use_cached_crf = false,
          .dry_run_falls_through = false,
          .extract_rpu = ctx->opt.scale_to_hd,
          .defer_shared_cleanup = false,
      };
      st = stage_rate_plan(ctx, &passhd);
      if (st != STAGE_CONTINUE) {
        rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
        goto out;
      }
      st = encode_pass(ctx, &passhd);
      if (st != STAGE_CONTINUE) {
        rc = (st == STAGE_EXIT_FAIL) ? 1 : 0;
        goto out;
      }
    } /* do_hd */
  }

  rc = ctx->pipeline_failed ? 1 : 0;
out:
  if (ctx->tracks.error == 0)
    free_media_tracks(&ctx->tracks);
  free(ctx);
  return rc;
}
