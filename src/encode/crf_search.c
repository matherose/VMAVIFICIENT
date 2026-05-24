/* CRF search via libSvtAv1Enc + libvmaf.
 *
 * For each trial CRF the search encodes short samples of the source
 * through encoder_svtav1, decodes the AV1 packets back to YUV with
 * lavc's AV1 decoder, and scores against the reference with
 * encoder_vmaf using harmonic-mean pooling. The search uses linear-
 * slope interpolation to converge on a CRF whose VMAF score is within
 * VMAV_CRF_CONVERGE_VMAF of the target.
 *
 * Sampling is adaptive: 1 sample for speed, escalating to 3 if the
 * 1-sample VMAF is clearly off-target. */

#include "vmavificient/vmav_crf_search.h"
#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_svtav1.h"
#include "vmavificient/vmav_vmaf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixfmt.h>

/* ====================================================================== */
/*  Pure math: sample picker + slope predictor + binary search            */
/* ====================================================================== */

static int round_clamp(double x, int lo, int hi) {
    int v = (int)lround(x);
    if (v < lo) {
        v = lo;
    }
    if (v > hi) {
        v = hi;
    }
    return v;
}

int vmav_crf_pick_samples(int64_t total_frames, int n, int frames_per_sample, int64_t *out_starts) {
    if (n < 1) {
        n = 1;
    }
    /* 10% margin at head/tail avoids title cards and end credits. */
    const int64_t margin = total_frames / 10;
    const int64_t lo = margin;
    const int64_t hi = total_frames - margin - frames_per_sample;
    if (hi <= lo) {
        out_starts[0] =
            total_frames > frames_per_sample ? (total_frames - frames_per_sample) / 2 : 0;
        return 1;
    }
    if (n == 1) {
        out_starts[0] = (lo + hi) / 2;
        return 1;
    }
    const int64_t step = (hi - lo) / (n - 1);
    for (int i = 0; i < n; i++) {
        out_starts[i] = lo + step * i;
    }
    return n;
}

int vmav_crf_next_guess(
    int crf_a, double vmaf_a, int crf_b, double vmaf_b, int target, int crf_min, int crf_max) {
    double slope;
    if (crf_b != crf_a && fabs(vmaf_b - vmaf_a) > 0.1) {
        slope = (vmaf_b - vmaf_a) / (double)(crf_b - crf_a);
    } else {
        slope = -VMAV_CRF_DEFAULT_SLOPE;
    }
    /* Slope must be negative (more CRF → less quality). Floor it. */
    if (slope > -0.05) {
        slope = -VMAV_CRF_DEFAULT_SLOPE;
    }
    const double next = (double)crf_b + ((double)target - vmaf_b) / slope;
    return round_clamp(next, crf_min, crf_max);
}

vmav_status_t vmav_crf_binary_search(int vmaf_target,
                                     int crf_min,
                                     int crf_max,
                                     int initial_crf,
                                     vmav_crf_score_fn fn,
                                     void *userdata,
                                     int *out_crf,
                                     double *out_vmaf) {
    if (fn == NULL || out_crf == NULL || out_vmaf == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_crf_binary_search: null arg");
    }
    if (crf_min < 0 || crf_max < crf_min || initial_crf < crf_min || initial_crf > crf_max) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG,
                        "vmav_crf_binary_search: bad bounds [%d, %d] init %d",
                        crf_min,
                        crf_max,
                        initial_crf);
    }

    int history_crf[VMAV_CRF_MAX_TRIALS] = {0};
    double history_vmaf[VMAV_CRF_MAX_TRIALS] = {0};
    int n_history = 0;

    int next_crf = initial_crf;
    int best_crf = -1;
    double best_vmaf = 0.0;
    double best_dist = 1e9;
    bool best_meets = false;

    for (int trial = 0; trial < VMAV_CRF_MAX_TRIALS; trial++) {
        /* Stop if the predictor wants to re-evaluate a CRF we've
         * already tried — slope-based interpolation has stalled. */
        bool dup = false;
        for (int i = 0; i < n_history; i++) {
            if (history_crf[i] == next_crf) {
                dup = true;
                break;
            }
        }
        if (dup) {
            break;
        }

        double vmaf = 0.0;
        const vmav_status_t st = fn(next_crf, userdata, &vmaf);
        if (!vmav_status_ok(st)) {
            return st;
        }

        history_crf[n_history] = next_crf;
        history_vmaf[n_history] = vmaf;
        n_history++;

        const bool meets = vmaf >= (double)vmaf_target;
        const double dist = fabs(vmaf - (double)vmaf_target);

        /* Best CRF preference: highest CRF that still meets the target;
         * if no trial meets it yet, the closest score from below. */
        if (meets) {
            if (!best_meets || next_crf > best_crf) {
                best_meets = true;
                best_crf = next_crf;
                best_vmaf = vmaf;
                best_dist = dist;
            }
        } else if (!best_meets && dist < best_dist) {
            best_crf = next_crf;
            best_vmaf = vmaf;
            best_dist = dist;
        }

        if (dist <= VMAV_CRF_CONVERGE_VMAF) {
            *out_crf = next_crf;
            *out_vmaf = vmaf;
            return VMAV_OK_STATUS;
        }

        if (n_history >= 2) {
            next_crf = vmav_crf_next_guess(history_crf[n_history - 2],
                                           history_vmaf[n_history - 2],
                                           history_crf[n_history - 1],
                                           history_vmaf[n_history - 1],
                                           vmaf_target,
                                           crf_min,
                                           crf_max);
        } else {
            const double delta = (vmaf - (double)vmaf_target) / VMAV_CRF_DEFAULT_SLOPE;
            next_crf = round_clamp((double)next_crf + delta, crf_min, crf_max);
        }
    }

    if (best_crf < 0) {
        return VMAV_ERR(VMAV_ERR_GENERIC, "binary_search: no usable trial");
    }
    *out_crf = best_crf;
    *out_vmaf = best_vmaf;
    return VMAV_OK_STATUS;
}

/* ====================================================================== */
/*  Source pipeline                                                       */
/* ====================================================================== */

typedef struct {
    AVFormatContext *ifmt;
    AVCodecContext *dec;
    int video_idx;
    AVRational fps;
    int64_t total_frames;
    int width;
    int height;
    int bit_depth;
    AVStream *vstream;
} source_t;

static void source_close(source_t *s) {
    if (s == NULL) {
        return;
    }
    if (s->dec != NULL) {
        avcodec_free_context(&s->dec);
    }
    if (s->ifmt != NULL) {
        avformat_close_input(&s->ifmt);
    }
    memset(s, 0, sizeof(*s));
}

static vmav_status_t source_open(const char *path, source_t *s) {
    memset(s, 0, sizeof(*s));
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    int rc = avformat_open_input(&s->ifmt, path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        return VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", path, errbuf);
    }
    rc = avformat_find_stream_info(s->ifmt, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        source_close(s);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "stream info: %s", errbuf);
    }
    s->video_idx = av_find_best_stream(s->ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (s->video_idx < 0) {
        source_close(s);
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "no video stream in '%s'", path);
    }
    s->vstream = s->ifmt->streams[s->video_idx];
    const AVCodec *codec = avcodec_find_decoder(s->vstream->codecpar->codec_id);
    if (codec == NULL) {
        source_close(s);
        return VMAV_ERR(VMAV_ERR_FFMPEG,
                        "no decoder for codec %s",
                        avcodec_get_name(s->vstream->codecpar->codec_id));
    }
    s->dec = avcodec_alloc_context3(codec);
    if (s->dec == NULL || avcodec_parameters_to_context(s->dec, s->vstream->codecpar) < 0 ||
        avcodec_open2(s->dec, codec, NULL) < 0) {
        source_close(s);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "decoder open failed");
    }
    s->fps = av_guess_frame_rate(s->ifmt, s->vstream, NULL);
    if (s->fps.num == 0) {
        s->fps = (AVRational){24000, 1001};
    }
    s->width = s->dec->width;
    s->height = s->dec->height;
    s->bit_depth =
        (s->dec->pix_fmt == AV_PIX_FMT_YUV420P10LE || s->dec->pix_fmt == AV_PIX_FMT_YUV420P10BE)
            ? 10
            : 8;
    s->total_frames =
        s->vstream->nb_frames > 0
            ? s->vstream->nb_frames
            : (int64_t)(av_q2d(s->fps) * ((double)s->ifmt->duration / (double)AV_TIME_BASE));
    return VMAV_OK_STATUS;
}

static vmav_status_t source_seek(source_t *s, int64_t target_frame) {
    const double seconds = (double)target_frame / av_q2d(s->fps);
    const int64_t ts = (int64_t)(seconds / av_q2d(s->vstream->time_base));
    avcodec_flush_buffers(s->dec);
    if (av_seek_frame(s->ifmt, s->video_idx, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        return VMAV_ERR(VMAV_ERR_FFMPEG, "seek to frame %lld failed", (long long)target_frame);
    }
    return VMAV_OK_STATUS;
}

/* ====================================================================== */
/*  Probe encode + score for one (CRF, samples) trial                     */
/* ====================================================================== */

/* Convert AVFrame's plane pointers into the (planes[3], strides[3])
 * pair the encoder + vmaf wrappers expect. */
static void avframe_to_planes(const AVFrame *f, const uint8_t *planes[3], int strides[3]) {
    for (int p = 0; p < 3; p++) {
        planes[p] = f->data[p];
        strides[p] = f->linesize[p];
    }
}

/* Run one probe trial: encode N frames at the given CRF, decode the
 * AV1 packets back, and submit (ref, decoded) pairs to vmaf. */
typedef struct {
    source_t src;
    const vmav_crf_search_spec_t *spec;
    const int64_t *sample_starts;
    int nsamples;
    int frames_per_sample;
} probe_ctx_t;

/* Build a vmav_svtav1_spec_t for the probe encoder. Mirrors the
 * production config so the CRF picked here matches what the real
 * encode will produce. */
static void fill_svt_spec(vmav_svtav1_spec_t *out, const probe_ctx_t *ctx, int crf) {
    memset(out, 0, sizeof(*out));
    out->preset = ctx->spec->preset;
    out->width = ctx->src.width;
    out->height = ctx->src.height;
    out->bit_depth = ctx->src.bit_depth;
    out->fps_num = ctx->src.fps.num;
    out->fps_den = ctx->src.fps.den;
    out->film_grain = ctx->spec->film_grain;
    out->target_bitrate_kbps = 0; /* CRF mode */
    out->crf = crf;
    const AVCodecParameters *par = ctx->src.vstream->codecpar;
    out->color_primaries = par->color_primaries;
    out->color_trc = par->color_trc;
    out->color_space = par->color_space;
    out->color_range = par->color_range;
}

/* Drain at most one packet from the encoder into *pkts. Returns:
 *   VMAV_OK    — a packet was stored, *pkt_count advanced.
 *   AGAIN/EOF  — encoder is not yet ready / fully drained; caller decides.
 *   error      — propagate.
 *
 * Extracted so the per-frame drain and the post-EOS drain in
 * score_one_crf can share the storage discipline. We MUST interleave
 * recv with send during normal pumping: SVT-AV1's input queue is sized
 * around look_ahead_distance (~120 with our presets) and never reclaims
 * slots unless we drain output. With 480 frames/sample × N samples per
 * trial, a tight send-only loop deadlocks the encoder after ~130 frames
 * — main thread blocks in svt_get_empty_object waiting for a slot that
 * a worker would only free if we called recv. */
static vmav_status_t
try_drain_one_packet(vmav_svtav1_encoder_t *enc, AVPacket ***pkts, int *pkt_count, int *pkt_cap) {
    const uint8_t *data = NULL;
    size_t size = 0;
    int64_t pts = 0;
    bool is_key = false;
    vmav_status_t st = vmav_svtav1_encoder_recv(enc, &data, &size, &pts, &is_key);
    if (!vmav_status_ok(st)) {
        return st; /* AGAIN, EOF, or error — caller decides */
    }
    if (*pkt_count >= *pkt_cap) {
        const int new_cap = *pkt_cap == 0 ? 64 : *pkt_cap * 2;
        AVPacket **resized = realloc(*pkts, (size_t)new_cap * sizeof(AVPacket *));
        if (resized == NULL) {
            vmav_svtav1_encoder_release(enc);
            return VMAV_ERR(VMAV_ERR_NO_MEM, "realloc av1_pkts");
        }
        *pkts = resized;
        *pkt_cap = new_cap;
    }
    AVPacket *p = av_packet_alloc();
    if (p == NULL || av_new_packet(p, (int)size) < 0) {
        if (p != NULL) {
            av_packet_free(&p);
        }
        vmav_svtav1_encoder_release(enc);
        return VMAV_ERR(VMAV_ERR_NO_MEM, "av_packet for av1 byte buffer");
    }
    memcpy(p->data, data, size);
    p->pts = pts;
    p->flags = is_key ? AV_PKT_FLAG_KEY : 0;
    (*pkts)[(*pkt_count)++] = p;
    vmav_svtav1_encoder_release(enc);
    return VMAV_OK_STATUS;
}

/* Score one CRF over all probe samples. Returns the harmonic-mean
 * pooled VMAF score across every frame of every sample. */
static vmav_status_t score_one_crf(int crf, void *userdata, double *out_vmaf) {
    probe_ctx_t *ctx = userdata;
    vmav_svtav1_spec_t svt_spec;
    fill_svt_spec(&svt_spec, ctx, crf);

    vmav_svtav1_encoder_t *enc = NULL;
    vmav_status_t st = vmav_svtav1_encoder_open(&svt_spec, &enc);
    if (!vmav_status_ok(st)) {
        return st;
    }

    vmav_vmaf_spec_t vmaf_spec = {
        .width = ctx->src.width,
        .height = ctx->src.height,
        .bit_depth = ctx->src.bit_depth,
        .n_threads = 4,
        .model = NULL,
    };
    vmav_vmaf_t *m = NULL;
    st = vmav_vmaf_open(&vmaf_spec, &m);
    if (!vmav_status_ok(st)) {
        vmav_svtav1_encoder_close(enc);
        return st;
    }

    /* Reference frames buffered for VMAF. Decoded packets are matched
     * to references by submission order. */
    AVFrame **ref_frames = NULL;
    int ref_count = 0;
    int ref_cap = 0;
    /* AV1 packets emitted by the encoder, decoded later to match against
     * reference frames. */
    AVPacket **av1_pkts = NULL;
    int pkt_count = 0;
    int pkt_cap = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *ref = av_frame_alloc();
    if (pkt == NULL || ref == NULL) {
        st = VMAV_ERR(VMAV_ERR_NO_MEM, "av_packet/av_frame alloc");
        goto cleanup;
    }

    /* For each sample: seek + decode `frames_per_sample` reference
     * frames, push each to the encoder, drain encoded AV1 packets. */
    for (int si = 0; si < ctx->nsamples; si++) {
        st = source_seek(&ctx->src, ctx->sample_starts[si]);
        if (!vmav_status_ok(st)) {
            goto cleanup;
        }
        int frames_done = 0;
        while (frames_done < ctx->frames_per_sample && av_read_frame(ctx->src.ifmt, pkt) >= 0) {
            if (pkt->stream_index != ctx->src.video_idx) {
                av_packet_unref(pkt);
                continue;
            }
            (void)avcodec_send_packet(ctx->src.dec, pkt);
            av_packet_unref(pkt);
            while (avcodec_receive_frame(ctx->src.dec, ref) == 0) {
                if (frames_done >= ctx->frames_per_sample) {
                    av_frame_unref(ref);
                    break;
                }
                /* Stash a clone of the reference for later VMAF pairing. */
                if (ref_count >= ref_cap) {
                    ref_cap = ref_cap == 0 ? 64 : ref_cap * 2;
                    AVFrame **resized = realloc(ref_frames, (size_t)ref_cap * sizeof(*ref_frames));
                    if (resized == NULL) {
                        av_frame_unref(ref);
                        st = VMAV_ERR(VMAV_ERR_NO_MEM, "realloc ref_frames");
                        goto cleanup;
                    }
                    ref_frames = resized;
                }
                ref_frames[ref_count] = av_frame_clone(ref);
                if (ref_frames[ref_count] == NULL) {
                    av_frame_unref(ref);
                    st = VMAV_ERR(VMAV_ERR_NO_MEM, "av_frame_clone");
                    goto cleanup;
                }
                /* Push the reference into the encoder. */
                const uint8_t *planes[3];
                int strides[3];
                avframe_to_planes(ref, planes, strides);
                st = vmav_svtav1_encoder_send(enc, planes, strides, ref->pts, /*eos=*/false);
                if (!vmav_status_ok(st)) {
                    av_frame_unref(ref);
                    goto cleanup;
                }
                ref_count++;
                frames_done++;
                av_frame_unref(ref);

                /* Drain whatever is ready right now. AGAIN means
                 * "no more output buffered, keep feeding"; we must
                 * NOT block here or SVT would never get more input. */
                for (;;) {
                    const vmav_status_t rs =
                        try_drain_one_packet(enc, &av1_pkts, &pkt_count, &pkt_cap);
                    if (vmav_status_ok(rs)) {
                        continue;
                    }
                    if (rs.code == VMAV_ERR_AGAIN) {
                        break;
                    }
                    if (rs.code == VMAV_ERR_EOF) {
                        /* Shouldn't happen pre-EOS; if it does, we're
                         * done emitting and the trailing EOS drain
                         * will be a no-op. */
                        break;
                    }
                    st = rs;
                    goto cleanup;
                }
            }
        }
    }

    /* Signal EOS and drain remaining AV1 packets. After EOS, AGAIN
     * means "still working, keep polling" — different from the
     * pre-EOS path above where AGAIN means "stop draining now". */
    st = vmav_svtav1_encoder_send(enc, NULL, NULL, 0, /*eos=*/true);
    if (!vmav_status_ok(st)) {
        goto cleanup;
    }
    for (;;) {
        const vmav_status_t rs = try_drain_one_packet(enc, &av1_pkts, &pkt_count, &pkt_cap);
        if (vmav_status_ok(rs)) {
            continue;
        }
        if (rs.code == VMAV_ERR_AGAIN) {
            continue;
        }
        if (rs.code == VMAV_ERR_EOF) {
            st = VMAV_OK_STATUS;
            break;
        }
        st = rs;
        goto cleanup;
    }

    /* Decode AV1 packets back and submit (ref, decoded) pairs to vmaf.
     *
     * Explicitly request libdav1d by name. FFmpeg's `avcodec_find_decoder(
     * AV_CODEC_ID_AV1)` returns the FIRST registered AV1 decoder, which
     * since FFmpeg 5.0 is a parser-only stub that forwards to either a
     * hwaccel or to libdav1d / libaom-av1. With hwaccels disabled at
     * configure time (single-binary distribution + CPU-only requirement),
     * the stub fails with "Failed to get pixel format" on every frame
     * because no backing decoder is available. The libdav1d wrapper is
     * a complete software decoder and never tries to negotiate hwaccel
     * formats. */
    const AVCodec *av1_dec = avcodec_find_decoder_by_name("libdav1d");
    if (av1_dec == NULL) {
        st = VMAV_ERR(VMAV_ERR_FFMPEG,
                      "libdav1d AV1 decoder not built into FFmpeg "
                      "(check --enable-libdav1d in VmavThirdParty.cmake)");
        goto cleanup;
    }
    AVCodecContext *av1_ctx = avcodec_alloc_context3(av1_dec);
    if (av1_ctx == NULL || avcodec_open2(av1_ctx, av1_dec, NULL) < 0) {
        if (av1_ctx != NULL) {
            avcodec_free_context(&av1_ctx);
        }
        st = VMAV_ERR(VMAV_ERR_FFMPEG, "av1 decoder open");
        goto cleanup;
    }

    AVFrame *dist = av_frame_alloc();
    unsigned vmaf_index = 0;
    if (dist == NULL) {
        avcodec_free_context(&av1_ctx);
        st = VMAV_ERR(VMAV_ERR_NO_MEM, "av_frame_alloc dist");
        goto cleanup;
    }
    for (int i = 0; i < pkt_count; i++) {
        (void)avcodec_send_packet(av1_ctx, av1_pkts[i]);
        while (avcodec_receive_frame(av1_ctx, dist) == 0) {
            if (vmaf_index >= (unsigned)ref_count) {
                av_frame_unref(dist);
                continue;
            }
            const uint8_t *ref_p[3];
            const uint8_t *dist_p[3];
            int ref_s[3];
            int dist_s[3];
            avframe_to_planes(ref_frames[vmaf_index], (const uint8_t **)ref_p, ref_s);
            avframe_to_planes(dist, (const uint8_t **)dist_p, dist_s);
            st = vmav_vmaf_submit(m, ref_p, ref_s, dist_p, dist_s, vmaf_index);
            av_frame_unref(dist);
            if (!vmav_status_ok(st)) {
                avcodec_free_context(&av1_ctx);
                av_frame_free(&dist);
                goto cleanup;
            }
            vmaf_index++;
        }
    }
    /* Flush decoder. */
    (void)avcodec_send_packet(av1_ctx, NULL);
    while (avcodec_receive_frame(av1_ctx, dist) == 0) {
        if (vmaf_index >= (unsigned)ref_count) {
            av_frame_unref(dist);
            continue;
        }
        const uint8_t *ref_p[3];
        const uint8_t *dist_p[3];
        int ref_s[3];
        int dist_s[3];
        avframe_to_planes(ref_frames[vmaf_index], (const uint8_t **)ref_p, ref_s);
        avframe_to_planes(dist, (const uint8_t **)dist_p, dist_s);
        st = vmav_vmaf_submit(m, ref_p, ref_s, dist_p, dist_s, vmaf_index);
        av_frame_unref(dist);
        if (!vmav_status_ok(st)) {
            break;
        }
        vmaf_index++;
    }
    avcodec_free_context(&av1_ctx);
    av_frame_free(&dist);

    if (vmav_status_ok(st)) {
        st = vmav_vmaf_finalize(m, out_vmaf);
    }

cleanup:
    av_packet_free(&pkt);
    av_frame_free(&ref);
    if (ref_frames != NULL) {
        for (int i = 0; i < ref_count; i++) {
            av_frame_free(&ref_frames[i]);
        }
        free(ref_frames);
    }
    if (av1_pkts != NULL) {
        for (int i = 0; i < pkt_count; i++) {
            av_packet_free(&av1_pkts[i]);
        }
        free(av1_pkts);
    }
    vmav_vmaf_close(m);
    vmav_svtav1_encoder_close(enc);
    return st;
}

/* ====================================================================== */
/*  Public entry point                                                    */
/* ====================================================================== */

vmav_status_t vmav_crf_search(const vmav_crf_search_spec_t *spec, vmav_crf_search_result_t *out) {
    if (spec == NULL || out == NULL || spec->input_path == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_crf_search: null arg");
    }
    memset(out, 0, sizeof(*out));

    const int crf_min = spec->crf_min > 0 ? spec->crf_min : VMAV_CRF_MIN;
    const int crf_max = spec->crf_max > 0 ? spec->crf_max : VMAV_CRF_MAX;

    probe_ctx_t ctx = {0};
    ctx.spec = spec;
    ctx.frames_per_sample = VMAV_CRF_SAMPLE_FRAMES;
    vmav_status_t st = source_open(spec->input_path, &ctx.src);
    if (!vmav_status_ok(st)) {
        return st;
    }
    if (ctx.src.total_frames < VMAV_CRF_SAMPLE_FRAMES) {
        source_close(&ctx.src);
        return VMAV_ERR(VMAV_ERR_BAD_ARG,
                        "input too short for CRF search (< %d frames)",
                        VMAV_CRF_SAMPLE_FRAMES);
    }

    /* Pass 1 — 1 sample. */
    int64_t starts1[1];
    ctx.nsamples = vmav_crf_pick_samples(ctx.src.total_frames, 1, VMAV_CRF_SAMPLE_FRAMES, starts1);
    ctx.sample_starts = starts1;
    int crf1 = -1;
    double vmaf1 = 0.0;
    st = vmav_crf_binary_search(
        spec->vmaf_target, crf_min, crf_max, VMAV_CRF_INITIAL, score_one_crf, &ctx, &crf1, &vmaf1);
    if (!vmav_status_ok(st)) {
        source_close(&ctx.src);
        return st;
    }
    if (fabs(vmaf1 - (double)spec->vmaf_target) <= VMAV_CRF_ESCALATE_VMAF) {
        out->crf = crf1;
        out->vmaf = vmaf1;
        out->escalated = false;
        source_close(&ctx.src);
        return VMAV_OK_STATUS;
    }

    /* Pass 2 — 3 samples, anchored at the 1-sample CRF. */
    int64_t starts3[3];
    ctx.nsamples = vmav_crf_pick_samples(ctx.src.total_frames, 3, VMAV_CRF_SAMPLE_FRAMES, starts3);
    ctx.sample_starts = starts3;
    int crf3 = -1;
    double vmaf3 = 0.0;
    st = vmav_crf_binary_search(
        spec->vmaf_target, crf_min, crf_max, crf1, score_one_crf, &ctx, &crf3, &vmaf3);
    source_close(&ctx.src);
    if (!vmav_status_ok(st)) {
        return st;
    }
    out->crf = crf3;
    out->vmaf = vmaf3;
    out->escalated = true;
    return VMAV_OK_STATUS;
}
