#include "vmavificient/vmav_analysis.h"
#include "vmavificient/vmav_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>

/* Baseline m5 impl: feed ~120 frames from the start through
 * `signalstats` and average its YDIF (frame-to-frame Y-plane diff)
 * metadata. Phase 4 will swap this for the multi-window grav1synth
 * pipeline that v1 used. */

#define VMAV_GRAIN_PROBE_FRAMES 120

/* Empirical calibration from v1: signalstats.YDIF == 30 on a typical
 * grainy analog-film source. Normalize to score [0, 1]. */
#define VMAV_GRAIN_YDIF_REFERENCE 30.0

static vmav_status_t build_signalstats_graph(AVCodecContext *dec_ctx,
                                             AVRational tb,
                                             AVFilterGraph **out_graph,
                                             AVFilterContext **out_src,
                                             AVFilterContext **out_sink) {
    AVFilterGraph *g = avfilter_graph_alloc();
    if (g == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "avfilter_graph_alloc");
    }
    char args[256];
    snprintf(args,
             sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
             dec_ctx->width,
             dec_ctx->height,
             dec_ctx->pix_fmt,
             tb.num,
             tb.den);
    const AVFilter *src = avfilter_get_by_name("buffer");
    const AVFilter *sig = avfilter_get_by_name("signalstats");
    const AVFilter *snk = avfilter_get_by_name("buffersink");
    if (src == NULL || sig == NULL || snk == NULL) {
        avfilter_graph_free(&g);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "missing lavfi filter (buffer/signalstats/buffersink)");
    }
    AVFilterContext *src_ctx = NULL;
    AVFilterContext *sig_ctx = NULL;
    AVFilterContext *snk_ctx = NULL;
    if (avfilter_graph_create_filter(&src_ctx, src, "src", args, NULL, g) < 0 ||
        avfilter_graph_create_filter(&sig_ctx, sig, "stats", NULL, NULL, g) < 0 ||
        avfilter_graph_create_filter(&snk_ctx, snk, "sink", NULL, NULL, g) < 0 ||
        avfilter_link(src_ctx, 0, sig_ctx, 0) < 0 || avfilter_link(sig_ctx, 0, snk_ctx, 0) < 0 ||
        avfilter_graph_config(g, NULL) < 0) {
        avfilter_graph_free(&g);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "signalstats graph wiring failed");
    }
    *out_graph = g;
    *out_src = src_ctx;
    *out_sink = snk_ctx;
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_grain_analyze(const char *path, vmav_grain_score_t *out) {
    if (path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_grain_analyze: null arg");
    }
    memset(out, 0, sizeof(*out));

    AVFormatContext *fmt_ctx = NULL;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];

    int rc = avformat_open_input(&fmt_ctx, path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        return VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", path, errbuf);
    }
    rc = avformat_find_stream_info(fmt_ctx, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        avformat_close_input(&fmt_ctx);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "stream info '%s': %s", path, errbuf);
    }
    const int vidx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) {
        avformat_close_input(&fmt_ctx);
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "no video stream in '%s'", path);
    }

    AVStream *stream = fmt_ctx->streams[vidx];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (dec_ctx == NULL || avcodec_parameters_to_context(dec_ctx, stream->codecpar) < 0 ||
        avcodec_open2(dec_ctx, decoder, NULL) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "decoder open failed");
    }

    AVFilterGraph *graph = NULL;
    AVFilterContext *src_ctx = NULL;
    AVFilterContext *sink_ctx = NULL;
    vmav_status_t st =
        build_signalstats_graph(dec_ctx, stream->time_base, &graph, &src_ctx, &sink_ctx);
    if (!vmav_status_ok(st)) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return st;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    double ydif_sum = 0.0;
    int ydif_count = 0;
    int decoded = 0;

    while (pkt != NULL && frame != NULL && filt_frame != NULL &&
           decoded < VMAV_GRAIN_PROBE_FRAMES && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != vidx) {
            av_packet_unref(pkt);
            continue;
        }
        (void)avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);
        while (avcodec_receive_frame(dec_ctx, frame) == 0) {
            decoded++;
            (void)av_buffersrc_add_frame_flags(src_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
            av_frame_unref(frame);
            while (av_buffersink_get_frame(sink_ctx, filt_frame) >= 0) {
                const AVDictionaryEntry *e =
                    av_dict_get(filt_frame->metadata, "lavfi.signalstats.YDIF", NULL, 0);
                if (e != NULL && e->value != NULL) {
                    ydif_sum += strtod(e->value, NULL);
                    ydif_count++;
                }
                av_frame_unref(filt_frame);
            }
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    avfilter_graph_free(&graph);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    if (ydif_count > 0) {
        const double avg_ydif = ydif_sum / (double)ydif_count;
        double score = avg_ydif / VMAV_GRAIN_YDIF_REFERENCE;
        if (score < 0.0) {
            score = 0.0;
        } else if (score > 1.0) {
            score = 1.0;
        }
        out->score = score;
    }
    out->variance = 0.0; /* Phase 4 fills this via multi-window grav1synth. */
    return VMAV_OK_STATUS;
}
