/* `vmavificient encode <input>` — production encode orchestration.
 *
 * Pipeline (minimum viable for Phase 4 m7):
 *   1. Probe input  via vmav_media_probe
 *   2. Pick preset  via --preset flag (default: live-action)
 *   3. CRF search   via vmav_crf_search (skipped if --crf is given)
 *   4. Video encode via vmav_video_encode
 *   5. Final mux    via vmav_final_mux
 *
 * Audio track encoding, subtitle conversion, DV RPU sidecar attachment,
 * and companion-HD output are wired up by Phase 4 m7's follow-up
 * (they consume vmav_audio_encode_track, vmav_subtitle_convert_pgs,
 * vmav_rpu_extract that already exist in tree). The minimum-viable
 * cut here proves the orchestration end-to-end. */

#include "vmavificient/vmav_crf_search.h"
#include "vmavificient/vmav_final_mux.h"
#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_media.h"
#include "vmavificient/vmav_preset.h"
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

    /* Final mux. Audio + subtitles deferred to follow-up; this run
     * produces a video-only MKV. */
    vmav_final_mux_spec_t mux_spec = {
        .video_path = ivf_path,
        .output_path = mkv_path,
        .chapters_source = args.input,
    };
    vmav_final_mux_result_t mux_result;
    st = vmav_final_mux(&mux_spec, &mux_result);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient encode: final_mux: %s\n", st.msg);
        return 1;
    }
    fprintf(stdout, "Encoded %s → %s (CRF %d", args.input, mkv_path, chosen_crf);
    if (chosen_vmaf > 0.0) {
        fprintf(stdout, ", VMAF %.2f", chosen_vmaf);
    }
    fputs(")\n", stdout);
    return 0;
}
