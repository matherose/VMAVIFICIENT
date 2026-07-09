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

  const char *filepath = ctx->opt.filepath;

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

  int bitrate = ctx->opt.bitrate; /* 0 if not specified; VBR only when set */

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
      char rpu_path[4096];
      snprintf(rpu_path, sizeof(rpu_path), "%s", ctx->rpu_path);

      /* Compute HD output dimensions from cropped source aspect ratio.
         Target width is always 1920; height is derived to preserve AR
         (rounded down to even for YUV420). */
      int hd_crop_w =
          ctx->info.width - (ctx->crop.error == 0 ? ctx->crop.left + ctx->crop.right : 0);
      int hd_crop_h =
          ctx->info.height - (ctx->crop.error == 0 ? ctx->crop.top + ctx->crop.bottom : 0);
      hd_crop_w = hd_crop_w & ~1;
      hd_crop_h = hd_crop_h & ~1;
      int hd_w = 1920;
      int hd_h = (int)((double)hd_crop_h * hd_w / hd_crop_w) & ~1;
      if (hd_h < 2)
        hd_h = 2;

      MediaInfo hd_info = ctx->info;
      hd_info.width = hd_w;
      hd_info.height = hd_h;

      /* DV Profile 8.1 is resolution-independent; keep it for HD. */
      HdrInfo hd_hdr = ctx->hdr;

      const EncodePreset *hd_preset = get_encode_preset(ctx->opt.quality, hd_h);
      int hd_vmaf_default = get_vmaf_target(ctx->opt.quality, hd_h);
      int hd_film_grain = get_film_grain_from_score(
          ctx->grain.error == 0 ? ctx->grain.grain_score : 0.0,
          ctx->grain.error == 0 ? ctx->grain.grain_variance : 0.0, ctx->opt.quality);

      /* HD output naming */
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
        /* blind mode: append -HDLight to input stem */
        snprintf(hd_base_name, sizeof(hd_base_name), "%s-HDLight", ctx->base_name);
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
          if (ctx->tracks.error == 0)
            free_media_tracks(&ctx->tracks);
          free(ctx);
          return 1;
        }
        char hd_csr_detail[96];
        snprintf(hd_csr_detail, sizeof(hd_csr_detail), "CRF %d  (VMAF %.2f, target %d)", hd_csr.crf,
                 hd_csr.vmaf_result, hd_vmaf_used);
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
      ui_kv("Scale", "%d×%d  (from %d×%d 4K source)", hd_w, hd_h, ctx->info.width,
            ctx->info.height);
      if (ctx->grain.error == 0) {
        int is_anim = (ctx->opt.quality == QUALITY_ANIMATION);
        const char *content_tier =
            is_anim ? "animation" : (ctx->grain.grain_score >= 0.08 ? "grainy" : "clean");
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
      ui_kv("Output", "%s%s", ctx->output_dir, hd_output_name);

      if (ctx->opt.grain_only)
        vmav_print_encoder_knobs(hd_preset, hd_film_grain);

      if (ctx->opt.dry_run || ctx->opt.grain_only) {
        ui_section(ctx->opt.grain_only ? "Grain-only" : "Dry run");
        ui_row("No files written. Re-run without %s to encode.",
               ctx->opt.grain_only ? "--grain-only" : "--dry-run");
        if (ctx->tracks.error == 0)
          free_media_tracks(&ctx->tracks);
        free(ctx);
        return 0;
      }
      ui_set_quiet(hd_saved_quiet);

      /* For --scale-to-hd the 4K encode block was skipped, so RPU hasn't
         been extracted yet.  Do it here before the HD video encode. */
      if (ctx->opt.scale_to_hd && ctx->hdr.error == 0 && ctx->hdr.has_dolby_vision) {
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
          .grain_score = ctx->grain.error == 0 ? ctx->grain.grain_score : 0.0,
          .grain_variance = ctx->grain.error == 0 ? ctx->grain.grain_variance : 0.0,
          .target_bitrate = bitrate,
          .crf = hd_crf,
          .info = &ctx->info, /* source 4K dims for decoder */
          .crop = (ctx->crop.error == 0) ? &ctx->crop : NULL,
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
        ctx->pipeline_failed = 1;
      }

      /* ---- HD final mux (reuse opus + srt from this session) ---- */
      {
        char hd_final_path[4096];
        snprintf(hd_final_path, sizeof(hd_final_path), "%s%s", ctx->output_dir, hd_output_name);

        MuxAudioTrack hd_mux_audio[32];
        for (int i = 0; i < ctx->opus_count && i < 32; i++) {
          hd_mux_audio[i].path = ctx->opus_paths[i];
          hd_mux_audio[i].language = ctx->audio_langs[i];
          hd_mux_audio[i].track_name = ctx->audio_names[i];
          hd_mux_audio[i].is_default = (i == 0) ? 1 : 0;
        }

        MuxSubtitleTrack hd_mux_subs[64];
        int hd_sub_default_set = 0;
        for (int i = 0; i < ctx->srt_count && i < 64; i++) {
          hd_mux_subs[i].path = ctx->srt_paths[i];
          hd_mux_subs[i].language = ctx->srt_langs[i];
          hd_mux_subs[i].track_name = ctx->srt_names[i];
          hd_mux_subs[i].is_forced = ctx->srt_is_forced[i];
          hd_mux_subs[i].is_sdh = ctx->srt_is_sdh[i];
          if (!hd_sub_default_set && ctx->srt_is_forced[i] &&
              (strcmp(ctx->srt_langs[i], "fre") == 0 || strcmp(ctx->srt_langs[i], "fra") == 0)) {
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
            .audio_count = ctx->opus_count,
            .subs = hd_mux_subs,
            .sub_count = ctx->srt_count,
            .title = ctx->mkv_title,
            .video_title = ctx->mkv_title,
            .video_language = ctx->video_language,
            .chapters_source_path = filepath,
        };

        ui_section(ctx->opt.companion_hd ? "HD final mux" : "Final mux");
        ui_kv("Inputs", "1 video + %d audio + %d subtitle track%s", ctx->opus_count, ctx->srt_count,
              ctx->srt_count == 1 ? "" : "s");
        time_t hd_mux_t0 = time(NULL);
        FinalMuxResult hd_mr = {.error = -1, .skipped = 0};

        if (ctx->audio_fail_count > 0) {
          char err[128];
          snprintf(err, sizeof(err), "%d audio track%s failed to encode — mux skipped",
                   ctx->audio_fail_count, ctx->audio_fail_count == 1 ? "" : "s");
          ui_stage_fail(hd_output_name, err);
          ui_hint("a release missing an announced audio track is broken; "
                  "cached intermediates are kept, fix the source and re-run");
          ctx->pipeline_failed = 1;
        } else if (hd_vr.error != 0) {
          ui_stage_fail(hd_output_name, "video encode failed — mux skipped");
          ctx->pipeline_failed = 1;
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
                     hd_mr.error, ctx->opus_count, ctx->srt_count);
            ui_stage_fail(hd_output_name, hd_mux_err);
            ui_hint("intermediates kept on disk; inspect them next to the "
                    "source file before re-running");
            ctx->pipeline_failed = 1;
          }
        }

        int hd_removed = 0;
        if (hd_mr.error == 0) {
          /* For scale-to-hd and companion-hd, remove shared audio/subtitle
             intermediates after HD mux. */
          if (ctx->opt.scale_to_hd || ctx->opt.companion_hd) {
            for (int i = 0; i < ctx->opus_count; i++)
              if (remove(ctx->opus_paths[i]) == 0)
                hd_removed++;
            for (int i = 0; i < ctx->srt_count; i++)
              if (strncmp(ctx->srt_paths[i], ctx->output_dir, strlen(ctx->output_dir)) == 0)
                if (remove(ctx->srt_paths[i]) == 0)
                  hd_removed++;
          }
          if (remove(hd_av1_video_path) == 0)
            hd_removed++;
          if (rpu_path[0] && remove(rpu_path) == 0)
            hd_removed++;
          if (hd_removed > 0) {
            char hd_clean_detail[64];
            snprintf(hd_clean_detail, sizeof(hd_clean_detail), "%d intermediate file%s", hd_removed,
                     hd_removed == 1 ? "" : "s");
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

          double hd_elapsed = difftime(time(NULL), ctx->encode_start_time);
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
  }

  rc = ctx->pipeline_failed ? 1 : 0;
out:
  if (ctx->tracks.error == 0)
    free_media_tracks(&ctx->tracks);
  free(ctx);
  return rc;
}
