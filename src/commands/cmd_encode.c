/* `vmavificient encode <input>` — production encode orchestration.
 *
 * Pipeline (resumable via <cache-dir>/state.json):
 *   1. Probe input          via vmav_media_probe.
 *   2. Enumerate tracks/HDR via vmav_media_tracks_probe + vmav_hdr_probe.
 *   3. Crop detection       via vmav_crop_probe       → state.crop.
 *   4. Grain analysis       via vmav_grain_analyze    → state.grain.
 *   5. CRF search           via vmav_crf_search       → state.crf.
 *      (or --crf bypass; either path persists the chosen CRF.)
 *   6. Audio encode         per track, vmav_audio_encode_track → state.audio[].
 *   7. Subtitle OCR         per PGS track, vmav_subtitle_convert_pgs
 *                                                     → state.subs[].
 *   8. DV RPU extract       best-effort sidecar if input is DV.
 *   9. Video encode         vmav_video_encode → IVF intermediate
 *                                                     → state.video.
 *  10. Final mux            vmav_final_mux → .mkv      → state.mux.
 *
 * Order rationale: audio + subs come BEFORE video so cheap-to-fail
 * steps (audio decode error, OCR crash, missing tessdata) surface in
 * minutes rather than after a multi-hour video encode commits.
 *
 * Each step checks state.X.status == COMPLETE before running. A
 * power-cut or Ctrl-C mid-encode is resumable: the next invocation
 * picks up at the first non-complete step.
 *
 * Still deferred:
 *   * Text-subtitle passthrough (SRT/ASS/VTT) — needs an ffmpeg-extract
 *     helper to pull the sub stream into a sidecar file.
 *   * DV RPU attachment to AV1 packets — currently we only extract the
 *     sidecar; injecting OBUs into encoded AV1 needs dovi_tool-style
 *     muxing in the encoder output path.
 *   * Companion-HD downscale output. */

#include "vmavificient/vmav_analysis.h"
#include "vmavificient/vmav_audio.h"
#include "vmavificient/vmav_crf_search.h"
#include "vmavificient/vmav_crop.h"
#include "vmavificient/vmav_encode_state.h"
#include "vmavificient/vmav_final_mux.h"
#include "vmavificient/vmav_hdr.h"
#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_media.h"
#include "vmavificient/vmav_naming.h"
#include "vmavificient/vmav_preset.h"
#include "vmavificient/vmav_rpu.h"
#include "vmavificient/vmav_subtitle.h"
#include "vmavificient/vmav_tracks.h"
#include "vmavificient/vmav_video_encode.h"

#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *input;
    const char *output;    /* explicit -o/--output; NULL → derived */
    const char *cache_dir; /* explicit --cache-dir; NULL → default */
    vmav_preset_t preset;
    int crf;         /* > 0 → use directly, skip CRF search */
    int target_vmaf; /* 0 → derive from preset + resolution */
    int crf_min;     /* 0 → use VMAV_CRF_MIN */
    int crf_max;     /* 0 → use VMAV_CRF_MAX */
    bool show_help;
} encode_args_t;

static void print_help(FILE *out) {
    fputs("Usage: vmavificient encode <input> [options]\n"
          "\n"
          "Encode <input> to AV1 inside a Matroska container.\n"
          "\n"
          "Options:\n"
          "  -o, --output <path>       Output .mkv path (default: derived)\n"
          "  --preset <name>           live-action | animation | super35_analog\n"
          "                            | super35_digital | imax_analog | imax_digital\n"
          "                            (default: live-action)\n"
          "  --target-vmaf <int>       VMAF target (default: per preset/resolution)\n"
          "  --vmaf-target <int>       Alias for --target-vmaf (v1 spelling)\n"
          "  --crf <int>               Use given CRF directly, skip CRF search\n"
          "  --crf-min <int>           Clamp CRF search lower bound\n"
          "  --crf-max <int>           Clamp CRF search upper bound\n"
          "  --cache-dir <path>        Where to keep intermediates +\n"
          "                            resume state (default:\n"
          "                            <input-dir>/.vmavificient-cache)\n"
          "  -h, --help                Show this help\n"
          "\n"
          "Quality preset shortcuts (each equivalent to --preset <name>):\n"
          "  --animation               Animation content\n"
          "  --super35_analog          Super 35mm analog film\n"
          "  --super35_digital         Super 35mm digital\n"
          "  --imax_analog             IMAX analog film\n"
          "  --imax_digital            IMAX digital\n",
          out);
}

/* Set the preset directly from a canonical name. Used by the
 * --animation / --super35_* / --imax_* shorthand flags. */
static vmav_status_t set_preset_shortcut(encode_args_t *out, const char *name) {
    return vmav_preset_from_string(name, &out->preset);
}

static vmav_status_t parse_args(int argc, char **argv, encode_args_t *out) {
    memset(out, 0, sizeof(*out));
    out->preset = VMAV_PRESET_LIVE_ACTION;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            out->show_help = true;
            return VMAV_OK_STATUS;
        }
        if ((strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) && i + 1 < argc) {
            out->output = argv[++i];
            continue;
        }
        if (strcmp(arg, "--preset") == 0 && i + 1 < argc) {
            const vmav_status_t st = vmav_preset_from_string(argv[++i], &out->preset);
            if (!vmav_status_ok(st)) {
                return st;
            }
            continue;
        }
        if ((strcmp(arg, "--target-vmaf") == 0 || strcmp(arg, "--vmaf-target") == 0) &&
            i + 1 < argc) {
            out->target_vmaf = atoi(argv[++i]);
            continue;
        }
        /* Preset shortcuts (v1 CLI parity). Each maps to --preset <name>. */
        if (strcmp(arg, "--animation") == 0) {
            const vmav_status_t st = set_preset_shortcut(out, "animation");
            if (!vmav_status_ok(st)) {
                return st;
            }
            continue;
        }
        if (strcmp(arg, "--super35_analog") == 0) {
            const vmav_status_t st = set_preset_shortcut(out, "super35_analog");
            if (!vmav_status_ok(st)) {
                return st;
            }
            continue;
        }
        if (strcmp(arg, "--super35_digital") == 0) {
            const vmav_status_t st = set_preset_shortcut(out, "super35_digital");
            if (!vmav_status_ok(st)) {
                return st;
            }
            continue;
        }
        if (strcmp(arg, "--imax_analog") == 0) {
            const vmav_status_t st = set_preset_shortcut(out, "imax_analog");
            if (!vmav_status_ok(st)) {
                return st;
            }
            continue;
        }
        if (strcmp(arg, "--imax_digital") == 0) {
            const vmav_status_t st = set_preset_shortcut(out, "imax_digital");
            if (!vmav_status_ok(st)) {
                return st;
            }
            continue;
        }
        if (strcmp(arg, "--crf") == 0 && i + 1 < argc) {
            out->crf = atoi(argv[++i]);
            continue;
        }
        if (strcmp(arg, "--crf-min") == 0 && i + 1 < argc) {
            out->crf_min = atoi(argv[++i]);
            continue;
        }
        if (strcmp(arg, "--crf-max") == 0 && i + 1 < argc) {
            out->crf_max = atoi(argv[++i]);
            continue;
        }
        if (strcmp(arg, "--cache-dir") == 0 && i + 1 < argc) {
            out->cache_dir = argv[++i];
            continue;
        }
        if (arg[0] == '-') {
            return VMAV_ERR(VMAV_ERR_BAD_ARG, "unknown flag: %s", arg);
        }
        /* Positional → input path. */
        if (out->input != NULL) {
            return VMAV_ERR(VMAV_ERR_BAD_ARG, "extra positional arg: %s", arg);
        }
        out->input = arg;
    }
    if (out->input == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "missing input file");
    }
    return VMAV_OK_STATUS;
}

/* Replace input's extension with `new_ext`. Returns the length, or 0
 * if the result overflows the destination buffer. */
static size_t replace_extension(char *buf, size_t cap, const char *path, const char *new_ext) {
    const size_t len = strlen(path);
    const char *dot = strrchr(path, '.');
    const size_t base_len = (dot != NULL && dot > path) ? (size_t)(dot - path) : len;
    const size_t ext_len = strlen(new_ext);
    if (base_len + ext_len + 1 >= cap) {
        return 0;
    }
    memcpy(buf, path, base_len);
    memcpy(buf + base_len, new_ext, ext_len);
    buf[base_len + ext_len] = '\0';
    return base_len + ext_len;
}

int cmd_encode_run(int argc, char **argv) {
    encode_args_t args;
    vmav_status_t st = parse_args(argc, argv, &args);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient encode: %s\n", st.msg);
        print_help(stderr);
        return 2;
    }
    if (args.show_help) {
        print_help(stdout);
        return 0;
    }

    /* Probe the input. */
    vmav_media_info_t info;
    st = vmav_media_probe(args.input, &info);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient encode: probe '%s': %s\n", args.input, st.msg);
        return 1;
    }

    /* Build derived output paths. */
    char ivf_path[1024];
    if (replace_extension(ivf_path, sizeof(ivf_path), args.input, ".av1.ivf") == 0) {
        fprintf(stderr, "vmavificient encode: input path too long\n");
        return 1;
    }
    char mkv_path[1024];
    if (args.output != NULL) {
        snprintf(mkv_path, sizeof(mkv_path), "%s", args.output);
    } else {
        if (replace_extension(mkv_path, sizeof(mkv_path), args.input, ".av1.mkv") == 0) {
            fprintf(stderr, "vmavificient encode: input path too long\n");
            return 1;
        }
    }

    /* Resolve cache_dir: explicit --cache-dir or
     * <dirname(input)>/.vmavificient-cache. */
    char cache_dir[1024];
    if (args.cache_dir != NULL) {
        snprintf(cache_dir, sizeof(cache_dir), "%s", args.cache_dir);
    } else {
        st = vmav_encode_state_default_cache_dir(args.input, cache_dir, sizeof(cache_dir));
        if (!vmav_status_ok(st)) {
            fprintf(stderr, "vmavificient encode: cache_dir: %s\n", st.msg);
            return 1;
        }
    }

    /* Load resume state. Fresh on first run or when input changed. */
    vmav_encode_state_t state;
    bool resumable = false;
    st = vmav_encode_state_load(cache_dir, args.input, &state, &resumable);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient encode: state_load: %s\n", st.msg);
        return 1;
    }
    if (resumable) {
        VMAV_LOG_INFO("encode: resuming from cache_dir=%s", cache_dir);
    } else {
        VMAV_LOG_INFO("encode: fresh state in cache_dir=%s", cache_dir);
    }

    /* ---- Step: crop detection ---- */
    if (state.crop.status != VMAV_STEP_COMPLETE) {
        VMAV_LOG_INFO("encode: detecting crop via lavfi cropdetect");
        vmav_crop_rect_t crop;
        const vmav_status_t cst = vmav_crop_probe(args.input, &crop);
        if (!vmav_status_ok(cst)) {
            fprintf(stderr, "vmavificient encode: crop_probe: %s\n", cst.msg);
            state.crop.status = VMAV_STEP_FAILED;
            (void)vmav_encode_state_save(cache_dir, &state);
            vmav_encode_state_free(&state);
            return 1;
        }
        state.crop.status = VMAV_STEP_COMPLETE;
        state.crop.x = crop.x;
        state.crop.y = crop.y;
        state.crop.width = crop.width;
        state.crop.height = crop.height;
        state.crop.is_meaningful = crop.is_meaningful;
        const vmav_status_t sst = vmav_encode_state_save(cache_dir, &state);
        if (!vmav_status_ok(sst)) {
            fprintf(stderr, "vmavificient encode: state_save (crop): %s\n", sst.msg);
            vmav_encode_state_free(&state);
            return 1;
        }
    }
    VMAV_LOG_INFO("encode: crop = (%d,%d) %dx%d (%s)",
                  state.crop.x,
                  state.crop.y,
                  state.crop.width,
                  state.crop.height,
                  state.crop.is_meaningful ? "trim black bars" : "no-op / passthrough");

    /* ---- Step: grain analysis ---- */
    if (state.grain.status != VMAV_STEP_COMPLETE) {
        VMAV_LOG_INFO("encode: analyzing grain via lavfi signalstats");
        vmav_grain_score_t grain;
        const vmav_status_t gst = vmav_grain_analyze(args.input, &grain);
        if (!vmav_status_ok(gst)) {
            fprintf(stderr, "vmavificient encode: grain_analyze: %s\n", gst.msg);
            state.grain.status = VMAV_STEP_FAILED;
            (void)vmav_encode_state_save(cache_dir, &state);
            vmav_encode_state_free(&state);
            return 1;
        }
        state.grain.status = VMAV_STEP_COMPLETE;
        state.grain.score = grain.score;
        state.grain.variance = grain.variance;
        const vmav_status_t sst = vmav_encode_state_save(cache_dir, &state);
        if (!vmav_status_ok(sst)) {
            fprintf(stderr, "vmavificient encode: state_save (grain): %s\n", sst.msg);
            vmav_encode_state_free(&state);
            return 1;
        }
    }
    VMAV_LOG_INFO(
        "encode: grain score = %.3f (variance=%.3f)", state.grain.score, state.grain.variance);

    /* === Up-front: tracks + naming + HDR + base path =====================
     * Hoisted ahead of the per-step blocks so subsequent steps (audio,
     * subs, RPU, mux) can reference them uniformly. */
    int exit_code = 0;

    /* path_buf_t typedef sidesteps a clang-format 18/22 disagreement on
     * the `char (*x)[N]` pointer-to-array declarator; both versions
     * format `path_buf_t *x` identically. */
    typedef char path_buf_t[1024];
    vmav_mux_audio_t *mux_audio = NULL;
    path_buf_t *audio_paths = NULL;
    vmav_mux_sub_t *mux_subs = NULL;
    path_buf_t *sub_paths = NULL;
    size_t audio_mux_count = 0;
    size_t sub_mux_count = 0;

    vmav_media_tracks_t tracks = {0};
    st = vmav_media_tracks_probe(args.input, &tracks);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient encode: tracks_probe: %s\n", st.msg);
        exit_code = 1;
        goto cleanup;
    }
    const vmav_naming_french_variant_t fv = vmav_naming_detect_french_variant(args.input);

    vmav_hdr_info_t hdr = {0};
    (void)vmav_hdr_probe(args.input, &hdr); /* best-effort; not fatal */

    /* Strip the .ivf from ivf_path to share a base with sidecar files:
     *   <base>.<lang>.opus
     *   <base>.<lang>.<full|forced|sdh>.srt
     *   <base>.rpu.bin */
    char base[1024];
    snprintf(base, sizeof(base), "%s", ivf_path);
    {
        char *dot = strrchr(base, '.');
        if (dot != NULL && strcmp(dot, ".ivf") == 0) {
            *dot = '\0';
        }
    }

    /* ---- Step: CRF search ----
     * Three paths:
     *   * state.crf already COMPLETE → reuse from cache
     *   * args.crf > 0 → user override, persist as the chosen value
     *   * else → run vmav_crf_search, persist result */
    if (state.crf.status != VMAV_STEP_COMPLETE) {
        if (args.crf > 0) {
            VMAV_LOG_INFO("encode: using user-provided CRF=%d (skipping search)", args.crf);
            state.crf.status = VMAV_STEP_COMPLETE;
            state.crf.crf = args.crf;
            state.crf.vmaf = 0.0;
            state.crf.escalated = false;
        } else {
            const int vmaf_target = args.target_vmaf > 0
                                        ? args.target_vmaf
                                        : vmav_preset_target_vmaf(args.preset, info.height);
            VMAV_LOG_INFO("encode: CRF search starting (preset=%s, target VMAF=%d, %dx%d)",
                          vmav_preset_name(args.preset),
                          vmaf_target,
                          info.width,
                          info.height);
            vmav_crf_search_spec_t spec = {
                .input_path = args.input,
                .preset = args.preset,
                .vmaf_target = vmaf_target,
                .crf_min = args.crf_min,
                .crf_max = args.crf_max,
            };
            vmav_crf_search_result_t r;
            const vmav_status_t cst = vmav_crf_search(&spec, &r);
            if (!vmav_status_ok(cst)) {
                fprintf(stderr, "vmavificient encode: crf_search: %s\n", cst.msg);
                state.crf.status = VMAV_STEP_FAILED;
                (void)vmav_encode_state_save(cache_dir, &state);
                exit_code = 1;
                goto cleanup;
            }
            state.crf.status = VMAV_STEP_COMPLETE;
            state.crf.crf = r.crf;
            state.crf.vmaf = r.vmaf;
            state.crf.escalated = r.escalated;
            VMAV_LOG_INFO("encode: crf_search result CRF=%d VMAF=%.2f (escalated=%s)",
                          r.crf,
                          r.vmaf,
                          r.escalated ? "yes" : "no");
        }
        const vmav_status_t sst = vmav_encode_state_save(cache_dir, &state);
        if (!vmav_status_ok(sst)) {
            fprintf(stderr, "vmavificient encode: state_save (crf): %s\n", sst.msg);
            exit_code = 1;
            goto cleanup;
        }
    } else {
        VMAV_LOG_INFO("encode: CRF=%d (from cache, VMAF=%.2f)", state.crf.crf, state.crf.vmaf);
    }
    const int chosen_crf = state.crf.crf;
    const double chosen_vmaf = state.crf.vmaf;

    /* ---- Step: audio (per-track) ----
     * For each track we look up its state entry by stream_index. If
     * COMPLETE, we trust the cached output_path; otherwise we encode
     * and append/update the state entry, then save. Failures abort
     * the whole encode — losing one audio track is not recoverable
     * in the current mux model. */
    if (tracks.audio_count > 0) {
        audio_paths = calloc(tracks.audio_count, sizeof(*audio_paths));
        if (audio_paths == NULL) {
            fprintf(stderr, "vmavificient encode: out of memory (audio_paths)\n");
            exit_code = 1;
            goto cleanup;
        }
        for (size_t i = 0; i < tracks.audio_count; i++) {
            const vmav_track_t *t = &tracks.audio[i];

            /* Look up existing state entry for this stream. */
            vmav_state_audio_t *st_audio = NULL;
            for (size_t j = 0; j < state.audio_count; j++) {
                if (state.audio[j].stream_index == t->stream_index) {
                    st_audio = &state.audio[j];
                    break;
                }
            }
            if (st_audio != NULL && st_audio->status == VMAV_STEP_COMPLETE) {
                snprintf(audio_paths[i], sizeof(audio_paths[i]), "%s", st_audio->output_path);
                VMAV_LOG_INFO(
                    "encode: audio[%zu] '%s' (cached) → %s", i, t->language, audio_paths[i]);
                continue;
            }

            vmav_audio_build_filename(
                audio_paths[i], sizeof(audio_paths[i]), base, t->language, fv);
            /* Disambiguate multi-track-same-language collisions
             * (Blu-rays often ship two English dubs; vmav_naming gives
             * one filename per <lang,fv> pair). */
            for (size_t j = 0; j < i; j++) {
                if (strcmp(audio_paths[j], audio_paths[i]) == 0) {
                    char *dot = strrchr(audio_paths[i], '.');
                    if (dot != NULL) {
                        char suffix[16];
                        const int n = snprintf(suffix, sizeof(suffix), ".%d", t->stream_index);
                        if (n > 0 && n < (int)sizeof(suffix) &&
                            strlen(audio_paths[i]) + (size_t)n + 1 < sizeof(audio_paths[i])) {
                            memmove(dot + n, dot, strlen(dot) + 1);
                            memcpy(dot, suffix, (size_t)n);
                        }
                    }
                    break;
                }
            }
            VMAV_LOG_INFO("encode: audio[%zu] '%s' (%dch %s) → %s",
                          i,
                          t->language,
                          t->channels,
                          t->codec_name,
                          audio_paths[i]);
            vmav_audio_encode_t aer;
            st = vmav_audio_encode_track(args.input, t, audio_paths[i], &aer);
            if (!vmav_status_ok(st)) {
                fprintf(stderr, "vmavificient encode: audio[%zu]: %s\n", i, st.msg);
                if (st_audio != NULL) {
                    st_audio->status = VMAV_STEP_FAILED;
                } else {
                    vmav_state_audio_t fail = {0};
                    fail.status = VMAV_STEP_FAILED;
                    fail.stream_index = t->stream_index;
                    (void)vmav_encode_state_add_audio(&state, &fail);
                }
                (void)vmav_encode_state_save(cache_dir, &state);
                exit_code = 1;
                goto cleanup;
            }

            vmav_state_audio_t entry = {0};
            entry.status = VMAV_STEP_COMPLETE;
            entry.stream_index = t->stream_index;
            snprintf(entry.language, sizeof(entry.language), "%s", t->language);
            snprintf(entry.title, sizeof(entry.title), "%s", t->name);
            entry.is_default = (i == 0); /* first track of source is default */
            snprintf(entry.output_path, sizeof(entry.output_path), "%s", audio_paths[i]);
            if (st_audio != NULL) {
                *st_audio = entry;
            } else {
                const vmav_status_t ast = vmav_encode_state_add_audio(&state, &entry);
                if (!vmav_status_ok(ast)) {
                    fprintf(stderr, "vmavificient encode: state_add_audio: %s\n", ast.msg);
                    exit_code = 1;
                    goto cleanup;
                }
            }
            const vmav_status_t sst = vmav_encode_state_save(cache_dir, &state);
            if (!vmav_status_ok(sst)) {
                fprintf(stderr, "vmavificient encode: state_save (audio): %s\n", sst.msg);
                exit_code = 1;
                goto cleanup;
            }
        }
    }

    /* ---- Step: subtitles (per-track PGS → SRT) ----
     * Text subs are skipped (passthrough not yet wired). PGS that OCRs
     * to zero events stays in state with subtitle_count=0 so resume
     * doesn't re-OCR; the mux loop below skips zero-count entries. */
    if (tracks.subtitle_count > 0) {
        sub_paths = calloc(tracks.subtitle_count, sizeof(*sub_paths));
        if (sub_paths == NULL) {
            fprintf(stderr, "vmavificient encode: out of memory (sub_paths)\n");
            exit_code = 1;
            goto cleanup;
        }
        for (size_t i = 0; i < tracks.subtitle_count; i++) {
            const vmav_track_t *t = &tracks.subtitle[i];
            if (!vmav_subtitle_is_pgs(t)) {
                VMAV_LOG_INFO("encode: subtitle[%zu] '%s' codec=%s — text-sub "
                              "passthrough not yet wired, skipping",
                              i,
                              t->language,
                              t->codec_name);
                continue;
            }

            vmav_state_sub_t *st_sub = NULL;
            for (size_t j = 0; j < state.sub_count; j++) {
                if (state.subs[j].stream_index == t->stream_index) {
                    st_sub = &state.subs[j];
                    break;
                }
            }
            if (st_sub != NULL && st_sub->status == VMAV_STEP_COMPLETE) {
                snprintf(sub_paths[i], sizeof(sub_paths[i]), "%s", st_sub->output_path);
                VMAV_LOG_INFO("encode: subtitle[%zu] PGS '%s' (cached, %d events) → %s",
                              i,
                              t->language,
                              st_sub->subtitle_count,
                              sub_paths[i]);
                continue;
            }

            vmav_subtitle_build_srt_filename(
                sub_paths[i], sizeof(sub_paths[i]), base, t->language, fv, t->is_forced, t->is_sdh);
            VMAV_LOG_INFO("encode: subtitle[%zu] PGS '%s' → %s", i, t->language, sub_paths[i]);
            vmav_subtitle_convert_t scr;
            st = vmav_subtitle_convert_pgs(args.input, t, sub_paths[i], NULL, &scr);
            if (!vmav_status_ok(st)) {
                fprintf(stderr, "vmavificient encode: subtitle[%zu]: %s\n", i, st.msg);
                if (st_sub != NULL) {
                    st_sub->status = VMAV_STEP_FAILED;
                } else {
                    vmav_state_sub_t fail = {0};
                    fail.status = VMAV_STEP_FAILED;
                    fail.stream_index = t->stream_index;
                    (void)vmav_encode_state_add_sub(&state, &fail);
                }
                (void)vmav_encode_state_save(cache_dir, &state);
                exit_code = 1;
                goto cleanup;
            }

            /* 0 OCR events: file may be empty (libav's srt demuxer
             * would reject it later) — remove it but persist the
             * status so resume doesn't redo the work. */
            if (scr.subtitle_count == 0 && !scr.skipped) {
                VMAV_LOG_INFO("encode: subtitle[%zu] PGS '%s' produced 0 events — file removed",
                              i,
                              t->language);
                remove(sub_paths[i]);
                sub_paths[i][0] = '\0'; /* mux loop ignores empty output_path */
            }

            vmav_state_sub_t entry = {0};
            entry.status = VMAV_STEP_COMPLETE;
            entry.stream_index = t->stream_index;
            snprintf(entry.language, sizeof(entry.language), "%s", t->language);
            entry.is_forced = t->is_forced;
            entry.is_sdh = t->is_sdh;
            snprintf(entry.output_path, sizeof(entry.output_path), "%s", sub_paths[i]);
            entry.subtitle_count = scr.subtitle_count;
            if (st_sub != NULL) {
                *st_sub = entry;
            } else {
                const vmav_status_t ast = vmav_encode_state_add_sub(&state, &entry);
                if (!vmav_status_ok(ast)) {
                    fprintf(stderr, "vmavificient encode: state_add_sub: %s\n", ast.msg);
                    exit_code = 1;
                    goto cleanup;
                }
            }
            const vmav_status_t sst = vmav_encode_state_save(cache_dir, &state);
            if (!vmav_status_ok(sst)) {
                fprintf(stderr, "vmavificient encode: state_save (sub): %s\n", sst.msg);
                exit_code = 1;
                goto cleanup;
            }
        }
    }

    /* ---- Step: DV RPU sidecar (no state — extract is cheap, rerunning
     *      on resume is harmless). final_mux doesn't yet inject the
     *      RPU into AV1 packets; we just stash the .rpu.bin next to
     *      the IVF so a future packaging step can attach it. ---- */
    if (hdr.has_dolby_vision) {
        char rpu_path[1024];
        vmav_rpu_build_filename(rpu_path, sizeof(rpu_path), base);
        VMAV_LOG_INFO("encode: input is DV — extracting RPU sidecar → %s", rpu_path);
        vmav_rpu_extract_t rer;
        const vmav_status_t rst = vmav_rpu_extract(args.input, rpu_path, &rer);
        if (!vmav_status_ok(rst)) {
            fprintf(stderr,
                    "vmavificient encode: rpu_extract: %s "
                    "(continuing without RPU sidecar)\n",
                    rst.msg);
        }
    }

    /* ---- Step: video encode ----
     * The most expensive computation. Order here is intentional:
     * audio + subs ran first so a failure there aborts before the
     * multi-hour video encode commits. */
    if (state.video.status != VMAV_STEP_COMPLETE) {
        vmav_video_encode_spec_t ve_spec = {
            .input_path = args.input,
            .output_path = ivf_path,
            .preset = args.preset,
            .crf = chosen_crf,
        };
        vmav_video_encode_result_t ve_result;
        st = vmav_video_encode(&ve_spec, &ve_result);
        if (!vmav_status_ok(st)) {
            fprintf(stderr, "vmavificient encode: video_encode: %s\n", st.msg);
            state.video.status = VMAV_STEP_FAILED;
            (void)vmav_encode_state_save(cache_dir, &state);
            exit_code = 1;
            goto cleanup;
        }
        VMAV_LOG_INFO("encode: video_encode wrote %lld bytes (%lld frames) to %s",
                      (long long)ve_result.bytes_written,
                      (long long)ve_result.frames_encoded,
                      ivf_path);
        state.video.status = VMAV_STEP_COMPLETE;
        snprintf(state.video.output_path, sizeof(state.video.output_path), "%s", ivf_path);
        state.video.frame_count = ve_result.frames_encoded;
        const vmav_status_t sst = vmav_encode_state_save(cache_dir, &state);
        if (!vmav_status_ok(sst)) {
            fprintf(stderr, "vmavificient encode: state_save (video): %s\n", sst.msg);
            exit_code = 1;
            goto cleanup;
        }
    } else {
        VMAV_LOG_INFO("encode: video_encode (cached) → %s [%lld frames]",
                      state.video.output_path,
                      (long long)state.video.frame_count);
    }

    /* ---- Step: final mux ----
     * Build the mux spec from state (the heap-allocated state entries
     * outlive this scope until vmav_encode_state_free in cleanup, so
     * pointing mux_audio[].path etc. at state.audio[].output_path is
     * safe). Skip subs whose output_path is empty (0-event PGS). */
    if (state.audio_count > 0) {
        mux_audio = calloc(state.audio_count, sizeof(*mux_audio));
        if (mux_audio == NULL) {
            fprintf(stderr, "vmavificient encode: out of memory (mux_audio)\n");
            exit_code = 1;
            goto cleanup;
        }
        for (size_t j = 0; j < state.audio_count; j++) {
            const vmav_state_audio_t *sa = &state.audio[j];
            if (sa->status != VMAV_STEP_COMPLETE) {
                continue;
            }
            mux_audio[audio_mux_count].path = sa->output_path;
            mux_audio[audio_mux_count].language = sa->language;
            mux_audio[audio_mux_count].track_name = sa->title[0] != '\0' ? sa->title : NULL;
            mux_audio[audio_mux_count].is_default = sa->is_default;
            audio_mux_count++;
        }
    }
    if (state.sub_count > 0) {
        mux_subs = calloc(state.sub_count, sizeof(*mux_subs));
        if (mux_subs == NULL) {
            fprintf(stderr, "vmavificient encode: out of memory (mux_subs)\n");
            exit_code = 1;
            goto cleanup;
        }
        for (size_t j = 0; j < state.sub_count; j++) {
            const vmav_state_sub_t *ss = &state.subs[j];
            if (ss->status != VMAV_STEP_COMPLETE) {
                continue;
            }
            if (ss->output_path[0] == '\0' || ss->subtitle_count == 0) {
                continue; /* 0-event SRT — skip from mux */
            }
            mux_subs[sub_mux_count].path = ss->output_path;
            mux_subs[sub_mux_count].language = ss->language;
            mux_subs[sub_mux_count].track_name = NULL;
            mux_subs[sub_mux_count].is_forced = ss->is_forced;
            mux_subs[sub_mux_count].is_sdh = ss->is_sdh;
            sub_mux_count++;
        }
    }

    if (state.mux.status != VMAV_STEP_COMPLETE) {
        vmav_final_mux_spec_t mux_spec = {
            .video_path = ivf_path,
            .output_path = mkv_path,
            .chapters_source = args.input,
            .audio = mux_audio,
            .audio_count = audio_mux_count,
            .subs = mux_subs,
            .sub_count = sub_mux_count,
        };
        vmav_final_mux_result_t mux_result;
        st = vmav_final_mux(&mux_spec, &mux_result);
        if (!vmav_status_ok(st)) {
            fprintf(stderr, "vmavificient encode: final_mux: %s\n", st.msg);
            state.mux.status = VMAV_STEP_FAILED;
            (void)vmav_encode_state_save(cache_dir, &state);
            exit_code = 1;
            goto cleanup;
        }
        state.mux.status = VMAV_STEP_COMPLETE;
        snprintf(state.mux.output_path, sizeof(state.mux.output_path), "%s", mkv_path);
        (void)vmav_encode_state_save(cache_dir, &state);
    } else {
        VMAV_LOG_INFO("encode: final_mux (cached) → %s", state.mux.output_path);
    }

    fprintf(stdout, "Encoded %s → %s (CRF %d", args.input, mkv_path, chosen_crf);
    if (chosen_vmaf > 0.0) {
        fprintf(stdout, ", VMAF %.2f", chosen_vmaf);
    }
    fprintf(stdout, ", %zu audio, %zu subs", audio_mux_count, sub_mux_count);
    if (hdr.has_dolby_vision) {
        fputs(", DV-RPU", stdout);
    }
    fputs(")\n", stdout);

cleanup:
    free(mux_audio);
    free(audio_paths);
    free(mux_subs);
    free(sub_paths);
    vmav_media_tracks_free(&tracks);
    vmav_encode_state_free(&state);
    return exit_code;
}
