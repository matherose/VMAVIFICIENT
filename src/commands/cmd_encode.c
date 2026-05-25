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
#include "vmavificient/vmav_config.h"
#include "vmavificient/vmav_crf_search.h"
#include "vmavificient/vmav_crop.h"
#include "vmavificient/vmav_encode_state.h"
#include "vmavificient/vmav_final_mux.h"
#include "vmavificient/vmav_hdr.h"
#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_media.h"
#include "vmavificient/vmav_naming.h"
#include "vmavificient/vmav_os.h"
#include "vmavificient/vmav_preset.h"
#include "vmavificient/vmav_rpu.h"
#include "vmavificient/vmav_subtitle.h"
#include "vmavificient/vmav_tmdb.h"
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
    int crf;          /* > 0 → use directly, skip CRF search */
    int bitrate;      /* > 0 → VBR mode at this kbps, skip CRF entirely */
    int target_vmaf;  /* 0 → derive from preset + resolution */
    int crf_min;      /* 0 → use VMAV_CRF_MIN */
    int crf_max;      /* 0 → use VMAV_CRF_MAX */
    int tmdb_id;      /* > 0 → look up TMDB; 0 with `blind` flag → skip */
    bool blind;       /* explicit "no TMDB lookup" */
    bool config_mode; /* --config → interactive setup, then exit */
    bool dry_run;     /* --dry-run → run through CRF + naming, print plan, exit */
    bool grain_only;  /* --grain-only → like --dry-run + dump preset knobs */
    bool quiet;       /* --quiet → vmav_log level WARN+ only */
    bool verbose;     /* --verbose → vmav_log level DEBUG; SVT stderr stays on */
    bool show_help;
    /* Naming overrides — applied only when scene-style naming is in
     * effect (i.e. with --tmdb). Defaults of UNKNOWN/0 mean "use
     * vmav_naming_detect_* / vmav_naming_lang_tag". */
    vmav_naming_source_t source_override;
    bool source_override_set;
    vmav_naming_lang_tag_t lang_override;
    bool lang_override_set;
    /* Additional SRT sidecar files supplied via --srt. Repeatable.
     * These join the OCR'd subs at final-mux time; they don't go
     * through OCR and aren't tracked in state.subs[]. */
    const char **extra_srts; /* heap, freed in cleanup */
    size_t extra_srt_count;
    size_t extra_srt_cap;
} encode_args_t;

/* Lookup tables for the 12 source + 16 language CLI override flags.
 * Each row is a (flag, enum value) pair; the parser walks the table
 * linearly. Mirrors v1's CLI exactly for muscle-memory parity. */
typedef struct {
    const char *flag;
    vmav_naming_source_t value;
} source_flag_t;

static const source_flag_t k_source_flags[] = {
    {"--bdrip", VMAV_SOURCE_BDRIP},
    {"--bluray", VMAV_SOURCE_BLURAY},
    {"--remux", VMAV_SOURCE_REMUX},
    {"--dvdrip", VMAV_SOURCE_DVDRIP},
    {"--dvdremux", VMAV_SOURCE_DVDREMUX},
    {"--webrip", VMAV_SOURCE_WEBRIP},
    {"--webdl", VMAV_SOURCE_WEBDL},
    {"--web", VMAV_SOURCE_WEB},
    {"--hdtv", VMAV_SOURCE_HDTV},
    {"--hdrip", VMAV_SOURCE_HDRIP},
    {"--tvrip", VMAV_SOURCE_TVRIP},
    {"--vhsrip", VMAV_SOURCE_VHSRIP},
};

typedef struct {
    const char *flag;
    vmav_naming_lang_tag_t value;
} lang_flag_t;

static const lang_flag_t k_lang_flags[] = {
    {"--multi", VMAV_LT_MULTI},
    {"--multivfi", VMAV_LT_MULTI_VFI},
    {"--multivff", VMAV_LT_MULTI_VFF},
    {"--multivfq", VMAV_LT_MULTI_VFQ},
    {"--multivf2", VMAV_LT_MULTI_VF2},
    {"--multivof", VMAV_LT_MULTI_VOF},
    {"--dual_vfi", VMAV_LT_DUAL_VFI},
    {"--dual_vff", VMAV_LT_DUAL_VFF},
    {"--dual_vfq", VMAV_LT_DUAL_VFQ},
    {"--french", VMAV_LT_FRENCH},
    {"--vff", VMAV_LT_VFF},
    {"--vof", VMAV_LT_VOF},
    {"--truefrench", VMAV_LT_TRUEFRENCH},
    {"--vo", VMAV_LT_VO},
    {"--vost", VMAV_LT_VOST},
    {"--fansub", VMAV_LT_FANSUB},
};

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
          "  --bitrate <kbps>          VBR mode at given bitrate;\n"
          "                            skips CRF search entirely.\n"
          "                            Mutually exclusive with --crf.\n"
          "  --cache-dir <path>        Where to keep intermediates +\n"
          "                            resume state (default:\n"
          "                            <input-dir>/.vmavificient-cache)\n"
          "  --tmdb <id>               TMDB movie id; output uses\n"
          "                            scene-style name. Requires API\n"
          "                            key via $TMDB_API_KEY or config.\n"
          "  --blind                   Skip TMDB lookup; output as\n"
          "                            <input-stem>.av1.mkv.\n"
          "  --config                  Interactive setup of TMDB API key\n"
          "                            and release group, then exit.\n"
          "  --dry-run                 Run analysis (crop, grain, CRF\n"
          "                            search) + naming, print the plan,\n"
          "                            then exit. No files written.\n"
          "  --grain-only              Like --dry-run, plus dump the\n"
          "                            resolved preset metadata (grain\n"
          "                            mode, VMAF target, bitrate tiers).\n"
          "  --srt <path>              Attach an extra SRT sidecar to the\n"
          "                            mux. Repeatable. Joins OCR'd PGS\n"
          "                            subs.\n"
          "  --quiet                   Suppress informational log lines\n"
          "                            (level WARN+ only).\n"
          "  --verbose                 Verbose log output (level DEBUG).\n"
          "  -h, --help                Show this help\n"
          "\n"
          "Quality preset shortcuts (each equivalent to --preset <name>):\n"
          "  --animation               Animation content\n"
          "  --super35_analog          Super 35mm analog film\n"
          "  --super35_digital         Super 35mm digital\n"
          "  --imax_analog             IMAX analog film\n"
          "  --imax_digital            IMAX digital\n"
          "\n"
          "Source overrides (override filename-based auto-detection;\n"
          "only affects the output name when --tmdb is used):\n"
          "  --bdrip --bluray --remux --dvdrip --dvdremux\n"
          "  --webrip --webdl --web --hdtv --hdrip --tvrip --vhsrip\n"
          "\n"
          "Language tag overrides (same conditions as source overrides):\n"
          "  --multi --multivfi --multivff --multivfq --multivf2 --multivof\n"
          "  --dual_vfi --dual_vff --dual_vfq\n"
          "  --french --vff --vof --truefrench --vo --vost --fansub\n",
          out);
}

/* Set the preset directly from a canonical name. Used by the
 * --animation / --super35_* / --imax_* shorthand flags. */
static vmav_status_t set_preset_shortcut(encode_args_t *out, const char *name) {
    return vmav_preset_from_string(name, &out->preset);
}

/* Render the resolved encode plan to stdout — used by --dry-run and
 * --grain-only. Caller has run probe/tracks/hdr + crop/grain/CRF and
 * built mkv_path; we just turn the assembled state into something
 * humans can sanity-check before committing to a multi-hour encode. */
static void print_plan(const encode_args_t *args,
                       const vmav_encode_state_t *state,
                       const vmav_media_info_t *info,
                       const vmav_hdr_info_t *hdr,
                       const vmav_media_tracks_t *tracks,
                       const char *mkv_path) {
    fputs("\n=== Encode plan ===\n", stdout);
    fprintf(stdout, "input         %s\n", args->input);
    fprintf(stdout, "output        %s\n", mkv_path);
    fprintf(stdout, "preset        %s\n", vmav_preset_name(args->preset));
    fprintf(stdout,
            "video         %dx%d @ %.3f fps, %s, bit_rate=%lld\n",
            info->width,
            info->height,
            info->framerate,
            info->codec_name,
            (long long)info->bit_rate);
    if (hdr->has_hdr10 || hdr->has_dolby_vision) {
        fprintf(stdout,
                "HDR           %s%s\n",
                hdr->has_hdr10 ? "HDR10 " : "",
                hdr->has_dolby_vision ? "DolbyVision " : "");
    } else {
        fputs("HDR           SDR\n", stdout);
    }
    fprintf(stdout,
            "crop          (%d,%d) %dx%d%s\n",
            state->crop.x,
            state->crop.y,
            state->crop.width,
            state->crop.height,
            state->crop.is_meaningful ? " (will trim)" : " (no-op)");
    fprintf(stdout,
            "grain         score=%.3f variance=%.3f\n",
            state->grain.score,
            state->grain.variance);
    if (state->crf.bitrate_kbps > 0) {
        fprintf(stdout, "rate control  VBR @ %d kbps\n", state->crf.bitrate_kbps);
    } else {
        fprintf(stdout,
                "rate control  CRF %d (VMAF=%.2f%s)\n",
                state->crf.crf,
                state->crf.vmaf,
                state->crf.escalated ? ", escalated" : "");
    }
    if (state->tmdb.tmdb_id > 0) {
        fprintf(stdout,
                "TMDB          %s (%d) [id=%d, lang=%s]\n",
                state->tmdb.title,
                state->tmdb.year,
                state->tmdb.tmdb_id,
                state->tmdb.original_lang);
    } else {
        fputs("TMDB          (skipped, blind naming)\n", stdout);
    }
    fprintf(stdout, "audio tracks  %zu\n", tracks->audio_count);
    for (size_t i = 0; i < tracks->audio_count; i++) {
        const vmav_track_t *t = &tracks->audio[i];
        fprintf(stdout,
                "  [%zu] %s %s %dch  %s\n",
                i,
                t->language,
                t->codec_name,
                t->channels,
                t->name[0] != '\0' ? t->name : "");
    }
    fprintf(stdout, "sub tracks    %zu\n", tracks->subtitle_count);
    for (size_t i = 0; i < tracks->subtitle_count; i++) {
        const vmav_track_t *t = &tracks->subtitle[i];
        fprintf(stdout,
                "  [%zu] %s %s%s%s\n",
                i,
                t->language,
                t->codec_name,
                t->is_forced ? " forced" : "",
                t->is_sdh ? " SDH" : "");
    }
    fputs("===================\n", stdout);
}

/* --grain-only adds a preset-knobs dump to the --dry-run plan. We
 * print the user-visible metadata (grain mode, VMAF target, bitrate
 * tiers) from vmav_preset_info — full SVT-AV1-HDR knob dump (50+
 * fields) is intentionally not exposed publicly yet. */
static void print_preset_dump(vmav_preset_t preset, int video_height, double grain_score) {
    const vmav_preset_info_t *p = vmav_preset_info(preset);
    if (p == NULL) {
        return;
    }
    fputs("\n=== Resolved preset knobs ===\n", stdout);
    fprintf(stdout, "name              %s (%s)\n", p->cli_name, p->display_name);
    fprintf(stdout,
            "grain mode        %s\n",
            p->grain_mode == VMAV_GRAIN_FILM ? "FILM (analyze + denoise + re-synth)"
                                             : "SYNTHETIC (content-agnostic overlay)");
    fprintf(stdout, "VMAF target       4K=%d, HD=%d\n", p->vmaf_target_4k, p->vmaf_target_hd);
    fprintf(stdout,
            "bitrate 4K kbps   grainy=%d clean=%d animation=%d\n",
            p->bitrate_4k_grainy,
            p->bitrate_4k_clean,
            p->bitrate_4k_animation);
    fprintf(stdout,
            "bitrate HD kbps   grainy=%d clean=%d animation=%d\n",
            p->bitrate_hd_grainy,
            p->bitrate_hd_clean,
            p->bitrate_hd_animation);
    fprintf(stdout,
            "resolved          target_vmaf=%d, target_bitrate=%d kbps\n",
            vmav_preset_target_vmaf(preset, video_height),
            vmav_preset_target_bitrate(preset, video_height, grain_score));
    fputs("=============================\n", stdout);
}

/* `--config` interactive setup. Loads the current config (if any) so
 * the user sees a preview, then prompts for new values. Empty input
 * keeps the existing value. Writes config.ini and returns. */
static int run_interactive_config(void) {
    vmav_config_t cfg;
    vmav_config_init(&cfg);
    /* Silently start from any existing file so we don't blow it away
     * if the user just wants to change one field. */
    (void)vmav_config_load(&cfg);

    char path[VMAV_PATH_MAX];
    if (vmav_status_ok(vmav_config_path(path, sizeof(path)))) {
        fprintf(stdout, "vmavificient config — writing to %s\n\n", path);
    }
    fprintf(stdout, "TMDB API key");
    if (cfg.tmdb_api_key[0] != '\0') {
        fprintf(stdout, " [%.4s…%s]", cfg.tmdb_api_key, strlen(cfg.tmdb_api_key) > 4 ? "" : "");
    }
    fputs(": ", stdout);
    fflush(stdout);
    char buf[256];
    if (fgets(buf, sizeof(buf), stdin) != NULL) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
        if (n > 0) {
            snprintf(cfg.tmdb_api_key, sizeof(cfg.tmdb_api_key), "%s", buf);
        }
    }

    fprintf(stdout, "Release group");
    if (cfg.release_group[0] != '\0') {
        fprintf(stdout, " [%s]", cfg.release_group);
    }
    fputs(": ", stdout);
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin) != NULL) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
        if (n > 0) {
            snprintf(cfg.release_group, sizeof(cfg.release_group), "%s", buf);
        }
    }

    const vmav_status_t st = vmav_config_save(&cfg);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient config: %s\n", st.msg);
        return 1;
    }
    fputs("\nSaved.\n", stdout);
    return 0;
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
        if (strcmp(arg, "--bitrate") == 0 && i + 1 < argc) {
            out->bitrate = atoi(argv[++i]);
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
        if (strcmp(arg, "--tmdb") == 0 && i + 1 < argc) {
            out->tmdb_id = atoi(argv[++i]);
            continue;
        }
        if (strcmp(arg, "--blind") == 0) {
            out->blind = true;
            continue;
        }
        if (strcmp(arg, "--config") == 0) {
            out->config_mode = true;
            continue;
        }
        if (strcmp(arg, "--dry-run") == 0) {
            out->dry_run = true;
            continue;
        }
        if (strcmp(arg, "--grain-only") == 0) {
            out->grain_only = true;
            continue;
        }
        if (strcmp(arg, "--quiet") == 0) {
            out->quiet = true;
            continue;
        }
        if (strcmp(arg, "--verbose") == 0) {
            out->verbose = true;
            continue;
        }
        if (strcmp(arg, "--srt") == 0 && i + 1 < argc) {
            if (out->extra_srt_count >= out->extra_srt_cap) {
                const size_t new_cap = out->extra_srt_cap == 0 ? 4 : out->extra_srt_cap * 2;
                const char **resized = realloc(out->extra_srts, new_cap * sizeof(*resized));
                if (resized == NULL) {
                    return VMAV_ERR(VMAV_ERR_NO_MEM, "--srt: realloc");
                }
                out->extra_srts = resized;
                out->extra_srt_cap = new_cap;
            }
            out->extra_srts[out->extra_srt_count++] = argv[++i];
            continue;
        }
        /* Source overrides (12 flags). */
        {
            bool matched = false;
            for (size_t k = 0; k < sizeof(k_source_flags) / sizeof(k_source_flags[0]); k++) {
                if (strcmp(arg, k_source_flags[k].flag) == 0) {
                    out->source_override = k_source_flags[k].value;
                    out->source_override_set = true;
                    matched = true;
                    break;
                }
            }
            if (matched) {
                continue;
            }
        }
        /* Language-tag overrides (16 flags). */
        {
            bool matched = false;
            for (size_t k = 0; k < sizeof(k_lang_flags) / sizeof(k_lang_flags[0]); k++) {
                if (strcmp(arg, k_lang_flags[k].flag) == 0) {
                    out->lang_override = k_lang_flags[k].value;
                    out->lang_override_set = true;
                    matched = true;
                    break;
                }
            }
            if (matched) {
                continue;
            }
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
    if (out->crf > 0 && out->bitrate > 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "--crf and --bitrate are mutually exclusive");
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
        /* `--config` doesn't take a positional input — parse_args
         * still errors on missing input even though we don't need it
         * for config mode. Detect it before printing help. */
        if (args.config_mode) {
            return run_interactive_config();
        }
        fprintf(stderr, "vmavificient encode: %s\n", st.msg);
        print_help(stderr);
        return 2;
    }
    if (args.show_help) {
        print_help(stdout);
        return 0;
    }
    if (args.config_mode) {
        return run_interactive_config();
    }

    /* Apply verbosity flags immediately so any subsequent log calls
     * honor them. --verbose wins if both are given, matching v1's
     * "they compose" doc (verbose overrides quiet for vmav_log; SVT
     * stderr is unaffected either way for now). */
    if (args.verbose) {
        vmav_log_set_level(VMAV_LL_DEBUG);
    } else if (args.quiet) {
        vmav_log_set_level(VMAV_LL_WARN);
    }

    /* Probe the input. */
    vmav_media_info_t info;
    st = vmav_media_probe(args.input, &info);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient encode: probe '%s': %s\n", args.input, st.msg);
        return 1;
    }

    /* IVF intermediate path is stable (derived from input). The MKV
     * output path is finalized AFTER the TMDB step — if TMDB returns
     * a title/year we build a scene-style filename; otherwise we
     * fall back to <input-stem>.av1.mkv. */
    char ivf_path[1024];
    if (replace_extension(ivf_path, sizeof(ivf_path), args.input, ".av1.ivf") == 0) {
        fprintf(stderr, "vmavificient encode: input path too long\n");
        return 1;
    }
    char mkv_path[1024];
    mkv_path[0] = '\0';
    if (args.output != NULL) {
        snprintf(mkv_path, sizeof(mkv_path), "%s", args.output);
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

    /* ---- Step: TMDB lookup ----
     * Three paths:
     *   * state.tmdb already COMPLETE → reuse (resume case)
     *   * args.blind OR no tmdb_id → record blind (tmdb_id=0)
     *   * tmdb_id > 0 → resolve API key + fetch movie, persist title/year
     * Failure to fetch when --tmdb was requested is fatal; failure to
     * resolve the API key is also fatal (can't lookup without it). */
    if (state.tmdb.status != VMAV_STEP_COMPLETE) {
        if (args.tmdb_id > 0 && !args.blind) {
            char api_key[128];
            const vmav_status_t kst = vmav_config_resolve_api_key(api_key, sizeof(api_key));
            if (!vmav_status_ok(kst)) {
                fprintf(stderr, "vmavificient encode: %s\n", kst.msg);
                vmav_encode_state_free(&state);
                return 1;
            }
            VMAV_LOG_INFO("encode: TMDB lookup id=%d", args.tmdb_id);
            vmav_tmdb_movie_t movie = {0};
            const vmav_status_t tst =
                vmav_tmdb_fetch_movie(args.tmdb_id, api_key, /*timeout_ms=*/0, &movie);
            if (!vmav_status_ok(tst)) {
                fprintf(stderr, "vmavificient encode: tmdb_fetch: %s\n", tst.msg);
                state.tmdb.status = VMAV_STEP_FAILED;
                (void)vmav_encode_state_save(cache_dir, &state);
                vmav_encode_state_free(&state);
                return 1;
            }
            state.tmdb.status = VMAV_STEP_COMPLETE;
            state.tmdb.tmdb_id = movie.tmdb_id;
            snprintf(state.tmdb.title, sizeof(state.tmdb.title), "%s", movie.original_title);
            state.tmdb.year = movie.release_year;
            snprintf(state.tmdb.original_lang,
                     sizeof(state.tmdb.original_lang),
                     "%s",
                     movie.original_language);
        } else {
            /* Blind: explicit or default. Persist tmdb_id=0 so resume
             * doesn't re-evaluate the flag combinations. */
            state.tmdb.status = VMAV_STEP_COMPLETE;
            state.tmdb.tmdb_id = 0;
        }
        const vmav_status_t sst = vmav_encode_state_save(cache_dir, &state);
        if (!vmav_status_ok(sst)) {
            fprintf(stderr, "vmavificient encode: state_save (tmdb): %s\n", sst.msg);
            vmav_encode_state_free(&state);
            return 1;
        }
    }
    if (state.tmdb.tmdb_id > 0) {
        VMAV_LOG_INFO("encode: TMDB = %s (%d, %s) [id=%d]",
                      state.tmdb.title,
                      state.tmdb.year,
                      state.tmdb.original_lang,
                      state.tmdb.tmdb_id);
    } else {
        VMAV_LOG_INFO("encode: TMDB skipped (blind naming)");
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
     * Four paths:
     *   * state.crf already COMPLETE → reuse from cache
     *   * args.bitrate > 0 → VBR override, skip CRF search entirely
     *   * args.crf > 0 → CRF override, persist as the chosen value
     *   * else → run vmav_crf_search, persist result */
    if (state.crf.status != VMAV_STEP_COMPLETE) {
        if (args.bitrate > 0) {
            VMAV_LOG_INFO("encode: using user-provided bitrate=%d kbps (VBR, skipping CRF search)",
                          args.bitrate);
            state.crf.status = VMAV_STEP_COMPLETE;
            state.crf.crf = 0;
            state.crf.bitrate_kbps = args.bitrate;
            state.crf.vmaf = 0.0;
            state.crf.escalated = false;
        } else if (args.crf > 0) {
            VMAV_LOG_INFO("encode: using user-provided CRF=%d (skipping search)", args.crf);
            state.crf.status = VMAV_STEP_COMPLETE;
            state.crf.crf = args.crf;
            state.crf.bitrate_kbps = 0;
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
            state.crf.bitrate_kbps = 0;
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
        if (state.crf.bitrate_kbps > 0) {
            VMAV_LOG_INFO("encode: bitrate=%d kbps (from cache, VBR mode)",
                          state.crf.bitrate_kbps);
        } else {
            VMAV_LOG_INFO("encode: CRF=%d (from cache, VMAF=%.2f)",
                          state.crf.crf,
                          state.crf.vmaf);
        }
    }
    const int chosen_crf = state.crf.crf;
    const int chosen_bitrate = state.crf.bitrate_kbps;
    const double chosen_vmaf = state.crf.vmaf;

    /* Finalize mkv_path early so --dry-run / --grain-only print the
     * actual path that a real encode would target. If args.output was
     * given, mkv_path is already set; if --tmdb completed, build a
     * scene-style name; else use the <input-stem>.av1.mkv fallback. */
    if (mkv_path[0] == '\0') {
        if (state.tmdb.tmdb_id > 0) {
            const vmav_naming_lang_tag_t lang_tag =
                args.lang_override_set
                    ? args.lang_override
                    : vmav_naming_lang_tag(&tracks, state.tmdb.original_lang, fv);
            const vmav_naming_source_t source = args.source_override_set
                                                    ? args.source_override
                                                    : vmav_naming_detect_source(args.input);
            vmav_config_t cfg;
            vmav_config_init(&cfg);
            (void)vmav_config_load(&cfg);
            const char *release_group = cfg.release_group[0] != '\0' ? cfg.release_group : "GROUP";
            const vmav_status_t nst = vmav_naming_build(mkv_path,
                                                        sizeof(mkv_path),
                                                        state.tmdb.title,
                                                        state.tmdb.year,
                                                        lang_tag,
                                                        info.height,
                                                        &hdr,
                                                        source,
                                                        release_group);
            if (!vmav_status_ok(nst)) {
                fprintf(stderr, "vmavificient encode: naming_build: %s\n", nst.msg);
                exit_code = 1;
                goto cleanup;
            }
        } else {
            if (replace_extension(mkv_path, sizeof(mkv_path), args.input, ".av1.mkv") == 0) {
                fprintf(stderr, "vmavificient encode: input path too long\n");
                exit_code = 1;
                goto cleanup;
            }
        }
        VMAV_LOG_INFO("encode: output path = %s", mkv_path);
    }

    /* ---- Dry-run / grain-only early exit ----
     * At this point we know everything the user cares about for the
     * "what would this encode do?" question. Print the plan, optional
     * preset dump, and skip every step that touches disk. */
    if (args.dry_run || args.grain_only) {
        print_plan(&args, &state, &info, &hdr, &tracks, mkv_path);
        if (args.grain_only) {
            print_preset_dump(args.preset, info.height, state.grain.score);
        }
        fputs("\n(dry-run, no files written)\n", stdout);
        exit_code = 0;
        goto cleanup;
    }

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
            .target_bitrate_kbps = chosen_bitrate,
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
    /* Sub slots = state.sub_count (OCR'd from input) + extra_srt_count
     * (user-supplied via --srt). Empty OCR slots are skipped at write
     * time. */
    const size_t total_sub_slots = state.sub_count + args.extra_srt_count;
    if (total_sub_slots > 0) {
        mux_subs = calloc(total_sub_slots, sizeof(*mux_subs));
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
        /* Append user-supplied --srt sidecars. We don't have language
         * metadata for these, so default to "und" and unset disposition
         * flags — the user can re-tag in mkvpropedit afterward if they
         * want specific languages or forced flags. */
        for (size_t j = 0; j < args.extra_srt_count; j++) {
            mux_subs[sub_mux_count].path = args.extra_srts[j];
            mux_subs[sub_mux_count].language = "und";
            mux_subs[sub_mux_count].track_name = NULL;
            mux_subs[sub_mux_count].is_forced = false;
            mux_subs[sub_mux_count].is_sdh = false;
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

    fprintf(stdout, "Encoded %s → %s (", args.input, mkv_path);
    if (chosen_bitrate > 0) {
        fprintf(stdout, "VBR %d kbps", chosen_bitrate);
    } else {
        fprintf(stdout, "CRF %d", chosen_crf);
        if (chosen_vmaf > 0.0) {
            fprintf(stdout, ", VMAF %.2f", chosen_vmaf);
        }
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
    free(args.extra_srts);
    vmav_media_tracks_free(&tracks);
    vmav_encode_state_free(&state);
    return exit_code;
}
