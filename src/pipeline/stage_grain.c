/**
 * @file stage_grain.c
 * @brief Grain analysis stage: compute or load grain scores, derive encode preset.
 */

#include <stdio.h>

#include "pipeline.h"
#include "ui.h"

StageStatus stage_grain(PipelineCtx *ctx) {
  const char *filepath = ctx->opt.filepath;
  int do_hd = ctx->opt.companion_hd || ctx->opt.scale_to_hd;

  /* ---- Grain analysis ---- */
  ui_section("Grain analysis");
  ctx->film_grain = 0;

  /* Check for cached scores (uses global cached_* variables) */
  ctx->scores_cached = vmav_load_cached_scores(ctx->scores_cache_path, &ctx->cached_grain_score,
                                               &ctx->cached_grain_variance, &ctx->cached_crf);

  if (ctx->scores_cached) {
    ctx->film_grain = get_film_grain_from_score(ctx->cached_grain_score, ctx->cached_grain_variance,
                                                ctx->opt.quality);
    ui_kv("Luma score", "%.4f  (from cache)", ctx->cached_grain_score);
    ui_kv("Chroma score", "%.4f", ctx->cached_grain_variance * 100); /* approximate */
    ui_kv("Variance", "%.4f  (from cache)", ctx->cached_grain_variance);
    ui_kv("Synth level", "%d  (from cache)", ctx->film_grain);
    ui_stage_ok("Grain analysis", "loaded from cache");
    /* Refresh timestamp when using cached scores */
    if (!ctx->opt.crf && !ctx->opt.bitrate) {
      vmav_save_cached_scores(ctx->scores_cache_path, ctx->cached_grain_score,
                              ctx->cached_grain_variance, ctx->cached_crf);
    }
  } else {
    ui_row("Sampling 4 windows (extends to 7 if variance is high)…");
    ctx->grain = get_grain_score(filepath);
    if (ctx->grain.error == 0) {
      ctx->film_grain = get_film_grain_from_score(ctx->grain.grain_score, ctx->grain.grain_variance,
                                                  ctx->opt.quality);
      /* Per-window OK/FAIL lines already printed by media_analysis as it
         worked; these summary kvs collect the aggregate signal. */
      ui_kv("Luma score", "%.4f  (max across windows)", ctx->grain.grain_score);
      ui_kv("Chroma score", "%.4f", ctx->grain.grain_score * 100); /* approximate */
      ui_kv("Variance", "%.4f%s", ctx->grain.grain_variance,
            ctx->grain.windows_succeeded > 4 ? "  (refinement triggered)" : "");
      ui_kv("Synth level", "%d  (0–50)", ctx->film_grain);
      /* Save grain scores to cache */
      if (!ctx->opt.crf && !ctx->opt.bitrate) {
        if (!vmav_save_cached_scores(ctx->scores_cache_path, ctx->grain.grain_score,
                                     ctx->grain.grain_variance, 0)) {
          fprintf(stderr, "Warning: failed to save scores to cache\n");
        } else {
          ui_stage_ok("Cache", "saved grain analysis scores");
        }
      }
    } else {
      char err[64];
      snprintf(err, sizeof(err), "all windows failed (last error %d)", ctx->grain.error);
      ui_stage_fail("Grain analysis", err);
      ui_hint("set VMAV_KEEP_GRAIN_TMP=1 to retain the per-window scratch "
              "files for inspection");
    }
  }

  ctx->enc_preset = get_encode_preset(ctx->opt.quality, ctx->info.height);

  if (do_hd && ctx->info.height < 2160) {
    ui_stage_fail("Source", "--companion-hd / --scale-to-hd requires a 4K source "
                            "(height >= 2160)");
    return STAGE_EXIT_FAIL;
  }

  return STAGE_CONTINUE;
}
