#include "vmavificient/vmav_hdr.h"
#include "vmavificient/vmav_log.h"

#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>

#define VMAV_HDR10PLUS_PROBE_FRAMES 30

static vmav_code_t code_from_averror(int rc) {
    switch (rc) {
    case AVERROR(ENOENT):
    case AVERROR(EACCES):
    case AVERROR(EISDIR):
        return VMAV_ERR_IO;
    case AVERROR(ENOMEM):
        return VMAV_ERR_NO_MEM;
    default:
        return VMAV_ERR_FFMPEG;
    }
}

static void detect_dolby_vision(const AVCodecParameters *codecpar, vmav_hdr_info_t *info) {
    for (int i = 0; i < codecpar->nb_coded_side_data; i++) {
        const AVPacketSideData *sd = &codecpar->coded_side_data[i];
        if (sd->type == AV_PKT_DATA_DOVI_CONF &&
            sd->size >= sizeof(AVDOVIDecoderConfigurationRecord)) {
            const AVDOVIDecoderConfigurationRecord *dovi =
                (const AVDOVIDecoderConfigurationRecord *)sd->data;
            info->has_dolby_vision = true;
            info->dv_profile = dovi->dv_profile;
            info->dv_level = dovi->dv_level;
            return;
        }
    }
}

static void detect_hdr10plus(AVFormatContext *fmt_ctx, int video_idx, vmav_hdr_info_t *info) {
    AVStream *stream = fmt_ctx->streams[video_idx];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == NULL) {
        return;
    }
    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (dec_ctx == NULL) {
        return;
    }
    (void)avcodec_parameters_to_context(dec_ctx, stream->codecpar);
    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) {
        avcodec_free_context(&dec_ctx);
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (pkt == NULL || frame == NULL) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        return;
    }

    int decoded = 0;
    while (decoded < VMAV_HDR10PLUS_PROBE_FRAMES && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != video_idx) {
            av_packet_unref(pkt);
            continue;
        }
        (void)avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(dec_ctx, frame) == 0) {
            decoded++;
            if (av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS) != NULL) {
                info->has_hdr10plus = true;
                av_frame_unref(frame);
                goto done;
            }
            av_frame_unref(frame);
        }
    }

done:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&dec_ctx);
}

vmav_status_t vmav_hdr_probe(const char *path, vmav_hdr_info_t *out) {
    if (path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_hdr_probe: null arg");
    }
    memset(out, 0, sizeof(*out));
    out->dv_profile = -1;
    out->dv_level = -1;

    AVFormatContext *fmt_ctx = NULL;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];

    int rc = avformat_open_input(&fmt_ctx, path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        return vmav_status_make(
            code_from_averror(rc), __FILE__, __LINE__, "open '%s': %s", path, errbuf);
    }

    rc = avformat_find_stream_info(fmt_ctx, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        avformat_close_input(&fmt_ctx);
        return vmav_status_make(
            code_from_averror(rc), __FILE__, __LINE__, "stream info '%s': %s", path, errbuf);
    }

    const int vidx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) {
        avformat_close_input(&fmt_ctx);
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "no video stream in '%s'", path);
    }

    const AVCodecParameters *codecpar = fmt_ctx->streams[vidx]->codecpar;
    detect_dolby_vision(codecpar, out);

    if (codecpar->color_trc == AVCOL_TRC_SMPTE2084) {
        out->has_hdr10 = true;
        detect_hdr10plus(fmt_ctx, vidx, out);
    }

    avformat_close_input(&fmt_ctx);
    return VMAV_OK_STATUS;
}
