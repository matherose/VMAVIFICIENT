/**
 * @file stage_subs.c
 * @brief Subtitle stage: extract text/PGS subtitles, add user SRTs, sort.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "media_tracks.h"
#include "pipeline.h"
#include "proc.h"
#include "srt_sanitize.h"
#include "subtitle_convert.h"
#include "ui.h"

StageStatus stage_subs(PipelineCtx *ctx) {
  const char *filepath = ctx->opt.filepath;

  /* ---- Subtitle processing ---- */
  int sub_split_fre = (ctx->resolved_lang_tag == LANG_TAG_MULTI_VF2) ? 1 : 0;

  if (ctx->tracks.error == 0 && ctx->tracks.subtitle_count > 0 && !ctx->opt.dry_run &&
      !ctx->opt.grain_only) {
    ui_section("Subtitles");
    int n_sub_process = 0;
    for (int i = 0; i < ctx->tracks.subtitle_count; i++)
      if (!ctx->tracks.subtitles[i].is_karaoke)
        n_sub_process++;
    ui_kv("Process", "%d source track%s", n_sub_process, n_sub_process == 1 ? "" : "s");

    for (int i = 0; i < ctx->tracks.subtitle_count && ctx->srt_count < 48; i++) {
      TrackInfo *sub = &ctx->tracks.subtitles[i];
      if (sub->is_karaoke)
        continue;
      const char *lang = sub->language[0] ? sub->language : "und";

      /* Per-track French variant so VF2 sources keep VFF and VFQ
         subtitles as separate .fre.fr.srt / .fre.ca.srt files. */
      FrenchVariant track_fv = ctx->fv;
      FrenchAudioOrigin track_origin = ctx->fr_audio_origin;
      int track_variant_key = 0;
      if (sub_split_fre && (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0)) {
        FrenchVariant detected = (FrenchVariant)detect_track_french_variant(sub);
        if (detected != FRENCH_VARIANT_UNKNOWN) {
          track_fv = detected;
          track_variant_key = (int)detected;
          if (detected == FRENCH_VARIANT_VFQ)
            track_origin = FRENCH_AUDIO_VFQ;
          else if (detected == FRENCH_VARIANT_VFI)
            track_origin = FRENCH_AUDIO_VFI;
          else
            track_origin = FRENCH_AUDIO_VFF;
        }
      }

      if (is_text_subtitle(sub)) {
        /* Text subtitle: extract directly to SRT via FFmpeg CLI */
        char srt_fname[2048];
        build_srt_filename(srt_fname, sizeof(srt_fname), ctx->base_name, lang, track_fv,
                           sub->is_forced, sub->is_sdh);

        /* Write SRT to cache directory */
        char srt_cache_path[4096];
        vmav_build_cache_path(ctx, srt_cache_path, sizeof(srt_cache_path), srt_fname);
        snprintf(ctx->srt_paths[ctx->srt_count], sizeof(ctx->srt_paths[0]), "%s", srt_cache_path);

        /* Build display name */
        build_subtitle_track_name(ctx->srt_names[ctx->srt_count], sizeof(ctx->srt_names[0]), lang,
                                  1, sub->is_forced, sub->is_sdh, track_origin);
        snprintf(ctx->srt_langs[ctx->srt_count], sizeof(ctx->srt_langs[0]), "%s", lang);
        ctx->srt_is_forced[ctx->srt_count] = sub->is_forced;
        ctx->srt_is_sdh[ctx->srt_count] = sub->is_sdh;
        ctx->srt_variant[ctx->srt_count] = track_variant_key;

        /* Check if SRT already exists */
        struct stat srt_st;
        if (stat(ctx->srt_paths[ctx->srt_count], &srt_st) == 0 && srt_st.st_size > 0) {
          ui_stage_skip(srt_fname, "already exists");
          ctx->srt_count++;
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
          vmav_cmd_arg(&c, ctx->srt_paths[ctx->srt_count]);

          ui_row("Extract  #%-2d  %s  %s  →  \"%s\"", sub->index, lang, sub->codec,
                 ctx->srt_names[ctx->srt_count]);

          int exit_code = vmav_run(c.argv);
          struct stat srt_out_st;
          if (exit_code == 0 && stat(ctx->srt_paths[ctx->srt_count], &srt_out_st) == 0 &&
              srt_out_st.st_size > 0) {
            /* ffmpeg's ASS→SRT conversion keeps font face/size overrides as
               <font> tags sized in ASS script pixels; SRT renderers read
               size= as points and draw giant text. Source SRT tracks
               (codec_arg "copy") are left as authored. */
            if (strcmp(codec_arg, "srt") == 0 &&
                srt_strip_font_tags_file(ctx->srt_paths[ctx->srt_count]) != 0)
              ui_hint("could not strip <font> tags from converted SRT");
            ui_stage_ok(srt_fname, NULL);
            ctx->srt_count++;
          } else if (exit_code == 0) {
            /* ffmpeg succeeded but wrote no events (e.g. a forced track
               with nothing to force in this cut). An empty SRT is not a
               valid mux input, so drop it. */
            (void)remove(ctx->srt_paths[ctx->srt_count]); /* best-effort cleanup */
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
        for (int j = 0; j < ctx->srt_count; j++) {
          if (strcmp(ctx->srt_langs[j], lang) == 0 && ctx->srt_variant[j] == track_variant_key &&
              ctx->srt_is_forced[j] == sub->is_forced && ctx->srt_is_sdh[j] == sub->is_sdh) {
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
        build_srt_filename(srt_fname, sizeof(srt_fname), ctx->base_name, lang, track_fv,
                           sub->is_forced, sub->is_sdh);

        /* Write SRT to cache directory */
        char srt_cache_path[4096];
        vmav_build_cache_path(ctx, srt_cache_path, sizeof(srt_cache_path), srt_fname);
        snprintf(ctx->srt_paths[ctx->srt_count], sizeof(ctx->srt_paths[0]), "%s", srt_cache_path);

        build_subtitle_track_name(ctx->srt_names[ctx->srt_count], sizeof(ctx->srt_names[0]), lang,
                                  1, sub->is_forced, sub->is_sdh, track_origin);
        snprintf(ctx->srt_langs[ctx->srt_count], sizeof(ctx->srt_langs[0]), "%s", lang);
        ctx->srt_is_forced[ctx->srt_count] = sub->is_forced;
        ctx->srt_is_sdh[ctx->srt_count] = sub->is_sdh;
        ctx->srt_variant[ctx->srt_count] = track_variant_key;

        ui_row("OCR      PGS #%-2d  %s  →  \"%s\"", sub->index, lang,
               ctx->srt_names[ctx->srt_count]);

        SubtitleConvertResult scr =
            convert_pgs_to_srt(filepath, sub, ctx->srt_paths[ctx->srt_count], NULL);

        if (scr.skipped) {
          ui_stage_skip(srt_fname, "already exists");
          ctx->srt_count++;
        } else if (scr.error == 0 && scr.subtitle_count > 0) {
          char detail[64];
          snprintf(detail, sizeof(detail), "%d subtitles", scr.subtitle_count);
          ui_stage_ok(srt_fname, detail);
          ctx->srt_count++;
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
  for (int i = 0; i < ctx->opt.extra_srt_count && ctx->srt_count < 64; i++) {
    snprintf(ctx->srt_paths[ctx->srt_count], sizeof(ctx->srt_paths[0]), "%s",
             ctx->opt.extra_srt_paths[i]);

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

    build_subtitle_track_name(ctx->srt_names[ctx->srt_count], sizeof(ctx->srt_names[0]), srt_lang,
                              1, forced, sdh, ctx->fr_audio_origin);
    snprintf(ctx->srt_langs[ctx->srt_count], sizeof(ctx->srt_langs[0]), "%s", srt_lang);
    ctx->srt_is_forced[ctx->srt_count] = forced;
    ctx->srt_is_sdh[ctx->srt_count] = sdh;
    ctx->srt_variant[ctx->srt_count] = 0;

    printf("  [SRT]  %s → \"%s\"\n", ctx->opt.extra_srt_paths[i], ctx->srt_names[ctx->srt_count]);
    ctx->srt_count++;
  }

  /* ---- Sort subtitles: French Forced → French → English → Others ---- */
  if (ctx->srt_count > 1) {
    int order_idx[64];
    int order_key[64];
    for (int i = 0; i < ctx->srt_count; i++) {
      order_idx[i] = i;
      order_key[i] = vmav_sub_sort_key(ctx->srt_langs[i], ctx->srt_is_forced[i]);
    }
    /* Stable insertion sort */
    for (int i = 1; i < ctx->srt_count; i++) {
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
    memcpy(tmp_paths, ctx->srt_paths, sizeof(ctx->srt_paths));
    memcpy(tmp_names, ctx->srt_names, sizeof(ctx->srt_names));
    memcpy(tmp_langs, ctx->srt_langs, sizeof(ctx->srt_langs));
    memcpy(tmp_forced, ctx->srt_is_forced, sizeof(tmp_forced));
    memcpy(tmp_sdh, ctx->srt_is_sdh, sizeof(tmp_sdh));
    for (int i = 0; i < ctx->srt_count; i++) {
      int s = order_idx[i];
      memcpy(ctx->srt_paths[i], tmp_paths[s], sizeof(ctx->srt_paths[0]));
      memcpy(ctx->srt_names[i], tmp_names[s], sizeof(ctx->srt_names[0]));
      memcpy(ctx->srt_langs[i], tmp_langs[s], sizeof(ctx->srt_langs[0]));
      ctx->srt_is_forced[i] = tmp_forced[s];
      ctx->srt_is_sdh[i] = tmp_sdh[s];
    }
  }

  return STAGE_CONTINUE;
}
