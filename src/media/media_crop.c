#include "vmavificient/vmav_crop.h"
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
#include <libavutil/opt.h>

#define VMAV_CROP_PROBE_FRAMES 120
#define VMAV_CROP_MIN_MEANINGFUL_TRIM 8

/* Build the filter graph: buffer → cropdetect → buffersink. */
static vmav_status_t build_filter_graph(AVCodecContext *dec_ctx,
                                        AVRational time_base,
                                        AVFilterGraph **out_graph,
                                        AVFilterContext **out_src,
                                        AVFilterContext **out_sink) {
    AVFilterGraph *graph = avfilter_graph_alloc();
    if (graph == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "avfilter_graph_alloc");
    }
    char args[256];
    snprintf(args,
             sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
             dec_ctx->width,
             dec_ctx->height,
             dec_ctx->pix_fmt,
             time_base.num,
             time_base.den);
    const AVFilter *src_filter = avfilter_get_by_name("buffer");
    const AVFilter *sink_filter = avfilter_get_by_name("buffersink");
    const AVFilter *crop_filter = avfilter_get_by_name("cropdetect");
    if (src_filter == NULL || sink_filter == NULL || crop_filter == NULL) {
        avfilter_graph_free(&graph);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "missing lavfi filter (buffer/buffersink/cropdetect)");
    }

    AVFilterContext *src_ctx = NULL;
    AVFilterContext *crop_ctx = NULL;
    AVFilterContext *sink_ctx = NULL;
    if (avfilter_graph_create_filter(&src_ctx, src_filter, "src", args, NULL, graph) < 0 ||
        avfilter_graph_create_filter(
            &crop_ctx, crop_filter, "crop", "limit=24:round=2", NULL, graph) < 0 ||
        avfilter_graph_create_filter(&sink_ctx, sink_filter, "sink", NULL, NULL, graph) < 0 ||
        avfilter_link(src_ctx, 0, crop_ctx, 0) < 0 || avfilter_link(crop_ctx, 0, sink_ctx, 0) < 0 ||
        avfilter_graph_config(graph, NULL) < 0) {
        avfilter_graph_free(&graph);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "cropdetect graph wiring failed");
    }
    *out_graph = graph;
    *out_src = src_ctx;
    *out_sink = sink_ctx;
    return VMAV_OK_STATUS;
}

static void capture_latest_crop(AVFilterContext *sink, vmav_crop_rect_t *out) {
    AVFrame *frame = av_frame_alloc();
    if (frame == NULL) {
        return;
    }
    while (av_buffersink_get_frame(sink, frame) >= 0) {
        const AVDictionaryEntry *e = NULL;
        e = av_dict_iterate(frame->metadata, NULL);
        while (e != NULL) {
            if (strcmp(e->key, "lavfi.cropdetect.x") == 0) {
                out->x = atoi(e->value);
            } else if (strcmp(e->key, "lavfi.cropdetect.y") == 0) {
                out->y = atoi(e->value);
            } else if (strcmp(e->key, "lavfi.cropdetect.w") == 0) {
                out->width = atoi(e->value);
            } else if (strcmp(e->key, "lavfi.cropdetect.h") == 0) {
                out->height = atoi(e->value);
            }
            e = av_dict_iterate(frame->metadata, e);
        }
        av_frame_unref(frame);
    }
    av_frame_free(&frame);
}

vmav_status_t vmav_crop_probe(const char *path, vmav_crop_rect_t *out) {
    if (path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_crop_probe: null arg");
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
    vmav_status_t st = build_filter_graph(dec_ctx, stream->time_base, &graph, &src_ctx, &sink_ctx);
    if (!vmav_status_ok(st)) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return st;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int decoded = 0;
    while (pkt != NULL && frame != NULL && decoded < VMAV_CROP_PROBE_FRAMES &&
           av_read_frame(fmt_ctx, pkt) >= 0) {
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
            capture_latest_crop(sink_ctx, out);
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avfilter_graph_free(&graph);
    avcodec_free_context(&dec_ctx);

    const int src_w = stream->codecpar->width;
    const int src_h = stream->codecpar->height;

    /* Sanity: if cropdetect didn't fire we have zeros — fall back to
     * source dimensions. */
    if (out->width <= 0 || out->height <= 0) {
        out->x = 0;
        out->y = 0;
        out->width = src_w;
        out->height = src_h;
    }
    const int trim_x = src_w - out->width;
    const int trim_y = src_h - out->height;
    out->is_meaningful =
        trim_x >= VMAV_CROP_MIN_MEANINGFUL_TRIM || trim_y >= VMAV_CROP_MIN_MEANINGFUL_TRIM;

    avformat_close_input(&fmt_ctx);
    return VMAV_OK_STATUS;
}
