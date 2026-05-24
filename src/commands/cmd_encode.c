/* `vmavificient encode <input>` — production encode orchestration.
 *
 * Pipeline:
 *   1. Probe input    via vmav_media_probe (resolution, codec).
 *   2. Enumerate      via vmav_media_tracks_probe + vmav_hdr_probe.
 *   3. CRF search     via vmav_crf_search (skipped if --crf is given).
 *   4. Video encode   via vmav_video_encode → IVF intermediate.
 *   5. Audio encode   via vmav_audio_encode_track per audio track → .opus.
 *   6. Subtitle OCR   via vmav_subtitle_convert_pgs per PGS track → .srt.
 *   7. DV RPU extract via vmav_rpu_extract if input is Dolby Vision.
 *   8. Final mux      via vmav_final_mux → output .mkv.
 *
 * Still deferred:
 *   * Text-subtitle passthrough (SRT/ASS/VTT) — needs an ffmpeg-extract
 *     helper to pull the sub stream into a sidecar file.
 *   * DV RPU attachment to AV1 packets — currently we only extract the
 *     sidecar; injecting OBUs into encoded AV1 needs dovi_tool-style
 *     muxing in the encoder output path.
 *   * Companion-HD downscale output. */

#include "vmavificient/vmav_audio.h"
#include "vmavificient/vmav_crf_search.h"
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
    const char *output; /* explicit -o/--output; NULL → derived */
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
          "  --crf <int>               Use given CRF directly, skip CRF search\n"
          "  --crf-min <int>           Clamp CRF search lower bound\n"
          "  --crf-max <int>           Clamp CRF search upper bound\n"
          "  -h, --help                Show this help\n",
          out);
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
        if (strcmp(arg, "--target-vmaf") == 0 && i + 1 < argc) {
            out->target_vmaf = atoi(argv[++i]);
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

    /* Choose CRF: either user-provided or via CRF search. */
    int chosen_crf = args.crf;
    double chosen_vmaf = 0.0;
    if (chosen_crf <= 0) {
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
        st = vmav_crf_search(&spec, &r);
        if (!vmav_status_ok(st)) {
            fprintf(stderr, "vmavificient encode: crf_search: %s\n", st.msg);
            return 1;
        }
        chosen_crf = r.crf;
        chosen_vmaf = r.vmaf;
        VMAV_LOG_INFO("encode: crf_search result CRF=%d VMAF=%.2f (escalated=%s)",
                      chosen_crf,
                      chosen_vmaf,
                      r.escalated ? "yes" : "no");
    } else {
        VMAV_LOG_INFO("encode: using user-provided CRF=%d (skipping search)", chosen_crf);
    }

    /* Video encode. */
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
        return 1;
    }
    VMAV_LOG_INFO("encode: video_encode wrote %lld bytes (%lld frames) to %s",
                  (long long)ve_result.bytes_written,
                  (long long)ve_result.frames_encoded,
                  ivf_path);

    /* === Audio + subtitle + DV RPU side passes ============================
     * These all run side-by-side off the same source file. Each produces
     * a sidecar file that final_mux pulls into the output MKV. */

    int exit_code = 0;

    /* Declare every cleanup-touched local up-front so the `goto cleanup`
     * paths below never reach the free() block with garbage pointers
     * (Clang's -Wsometimes-uninitialized catches that otherwise).
     *
     * `path_buf_t` typedef avoids the awkward `char (*x)[N]`
     * pointer-to-array declarator, which clang-format 18 and 22
     * disagree on (CI runs 18). With the typedef both versions see
     * a plain `path_buf_t *x` and produce identical output. */
    typedef char path_buf_t[1024];
    vmav_mux_audio_t *mux_audio = NULL;
    path_buf_t *audio_paths = NULL;
    vmav_mux_sub_t *mux_subs = NULL;
    path_buf_t *sub_paths = NULL;
    size_t audio_mux_count = 0;
    size_t sub_mux_count = 0;

    /* Track enumeration + naming context (French variant from filename). */
    vmav_media_tracks_t tracks = {0};
    st = vmav_media_tracks_probe(args.input, &tracks);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient encode: tracks_probe: %s\n", st.msg);
        exit_code = 1;
        goto cleanup;
    }
    const vmav_naming_french_variant_t fv = vmav_naming_detect_french_variant(args.input);

    /* HDR probe — drives whether we need a DV RPU sidecar. */
    vmav_hdr_info_t hdr = {0};
    (void)vmav_hdr_probe(args.input, &hdr); /* best-effort; not fatal */

    /* Strip the .ivf to get a shared base for per-track outputs:
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

    /* --- Audio: one opus per audio track. --- */
    if (tracks.audio_count > 0) {
        mux_audio = calloc(tracks.audio_count, sizeof(*mux_audio));
        audio_paths = calloc(tracks.audio_count, sizeof(*audio_paths));
        if (mux_audio == NULL || audio_paths == NULL) {
            fprintf(stderr, "vmavificient encode: out of memory (audio)\n");
            exit_code = 1;
            goto cleanup;
        }
        for (size_t i = 0; i < tracks.audio_count; i++) {
            const vmav_track_t *t = &tracks.audio[i];
            vmav_audio_build_filename(
                audio_paths[i], sizeof(audio_paths[i]), base, t->language, fv);
            /* Disambiguate when multiple tracks share the same
             * language + French-variant (the canonical schema produces
             * one filename per <lang,fv> pair; a Blu-ray with two
             * English audio dubs would otherwise have the second one
             * overwrite the first). Inject `.<stream_index>` before
             * the .opus extension only when we'd collide. */
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
                exit_code = 1;
                goto cleanup;
            }
            mux_audio[audio_mux_count].path = audio_paths[i];
            mux_audio[audio_mux_count].language = t->language;
            mux_audio[audio_mux_count].track_name = t->name[0] != '\0' ? t->name : NULL;
            mux_audio[audio_mux_count].is_default = (audio_mux_count == 0);
            audio_mux_count++;
        }
    }

    /* --- Subtitles: PGS → SRT via OCR; text subs deferred (need ffmpeg
     *     stream-copy extraction, not in tree yet). --- */
    if (tracks.subtitle_count > 0) {
        mux_subs = calloc(tracks.subtitle_count, sizeof(*mux_subs));
        sub_paths = calloc(tracks.subtitle_count, sizeof(*sub_paths));
        if (mux_subs == NULL || sub_paths == NULL) {
            fprintf(stderr, "vmavificient encode: out of memory (subs)\n");
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
            vmav_subtitle_build_srt_filename(sub_paths[sub_mux_count],
                                             sizeof(sub_paths[sub_mux_count]),
                                             base,
                                             t->language,
                                             fv,
                                             t->is_forced,
                                             t->is_sdh);
            VMAV_LOG_INFO(
                "encode: subtitle[%zu] PGS '%s' → %s", i, t->language, sub_paths[sub_mux_count]);
            vmav_subtitle_convert_t scr;
            st = vmav_subtitle_convert_pgs(args.input,
                                           t,
                                           sub_paths[sub_mux_count],
                                           /*tess_lang=*/NULL,
                                           &scr);
            if (!vmav_status_ok(st)) {
                fprintf(stderr, "vmavificient encode: subtitle[%zu]: %s\n", i, st.msg);
                exit_code = 1;
                goto cleanup;
            }
            /* A forced-subtitle track on a short clip can legitimately
             * OCR to zero events (no forced caption appeared in this
             * sample window). libavformat's srt demuxer then rejects
             * the empty file as "invalid data", which would kill the
             * mux. Drop the empty sidecar and skip adding it to the
             * mux spec.
             *
             * `scr.subtitle_count` is left untouched when the convert
             * skipped (output_path existed with non-zero size from a
             * previous run), so we also need to honor `scr.skipped` —
             * otherwise a re-mux that reuses cached SRTs would drop
             * every subtitle track. */
            if (scr.subtitle_count == 0 && !scr.skipped) {
                VMAV_LOG_INFO(
                    "encode: subtitle[%zu] PGS '%s' produced 0 events — skipping (file removed)",
                    i,
                    t->language);
                remove(sub_paths[sub_mux_count]);
                continue;
            }
            mux_subs[sub_mux_count].path = sub_paths[sub_mux_count];
            mux_subs[sub_mux_count].language = t->language;
            mux_subs[sub_mux_count].track_name = t->name[0] != '\0' ? t->name : NULL;
            mux_subs[sub_mux_count].is_forced = t->is_forced;
            mux_subs[sub_mux_count].is_sdh = t->is_sdh;
            sub_mux_count++;
        }
    }

    /* --- Dolby Vision RPU sidecar (if input is DV). The current
     *     final_mux pipeline doesn't yet attach RPU to AV1 packets —
     *     that needs `dovi_tool inject-rpu`-style OBU injection.
     *     For now we just extract it so it's available beside the
     *     output for later attachment. --- */
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
            /* Non-fatal: we keep going so the user still gets a video
             * MKV — they can attach the RPU later if extraction works. */
        }
    }

    /* Final mux. */
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
        exit_code = 1;
        goto cleanup;
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
    return exit_code;
}
