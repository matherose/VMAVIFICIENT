#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_media.h"

#include <stdio.h>
#include <string.h>

#include <libavcodec/codec_par.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/rational.h>

/* Translate an AVERROR into one of our status codes. */
static vmav_code_t code_from_averror(int averr) {
    switch (averr) {
    case AVERROR(ENOENT):
    case AVERROR(EACCES):
    case AVERROR(EISDIR):
        return VMAV_ERR_IO;
    case AVERROR(ENOMEM):
        return VMAV_ERR_NO_MEM;
    case AVERROR_INVALIDDATA:
    case AVERROR_DEMUXER_NOT_FOUND:
    case AVERROR_DECODER_NOT_FOUND:
        return VMAV_ERR_PARSE;
    default:
        return VMAV_ERR_FFMPEG;
    }
}

static void copy_truncated(char *dst, size_t cap, const char *src) {
    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

vmav_status_t vmav_media_probe(const char *path, vmav_media_info_t *out) {
    if (path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_media_probe: null arg");
    }
    memset(out, 0, sizeof(*out));

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

    const AVStream *stream = fmt_ctx->streams[vidx];
    const AVCodecParameters *codecpar = stream->codecpar;

    out->video_stream_index = vidx;
    out->width = codecpar->width;
    out->height = codecpar->height;
    out->bit_rate = (int64_t)codecpar->bit_rate;

    if (fmt_ctx->duration > 0) {
        out->duration_s = (double)fmt_ctx->duration / (double)AV_TIME_BASE;
    }

    if (stream->avg_frame_rate.den > 0) {
        out->framerate = av_q2d(stream->avg_frame_rate);
    } else if (stream->r_frame_rate.den > 0) {
        out->framerate = av_q2d(stream->r_frame_rate);
    }

    copy_truncated(out->container_name, sizeof(out->container_name), fmt_ctx->iformat->name);
    copy_truncated(out->codec_name, sizeof(out->codec_name), avcodec_get_name(codecpar->codec_id));

    avformat_close_input(&fmt_ctx);
    return VMAV_OK_STATUS;
}

void vmav_media_info_log(const vmav_media_info_t *info, const char *path) {
    if (info == NULL) {
        return;
    }
    VMAV_LOG_INFO("media: %s — %s/%s %dx%d %.3f fps %.1f s",
                  path != NULL ? path : "(?)",
                  info->container_name,
                  info->codec_name,
                  info->width,
                  info->height,
                  info->framerate,
                  info->duration_s);
}
