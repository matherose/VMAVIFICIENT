#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_rpu.h"
#include "vmavificient/vmav_ui.h"

#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libdovi/rpu_parser.h>
#include <sys/stat.h>

/* HEVC NAL unit type for Dolby Vision RPU. */
#define HEVC_NAL_UNSPEC62 62

void vmav_rpu_build_filename(char *buf, size_t bufsize, const char *base_name) {
    if (buf == NULL || bufsize == 0 || base_name == NULL) {
        return;
    }
    snprintf(buf, bufsize, "%s.rpu.bin", base_name);
}

/* Scan length-prefixed (mp4/mkv) NAL units for UNSPEC62. */
static bool find_unspec62_length_prefixed(const uint8_t *data,
                                          int size,
                                          const uint8_t **out_nal,
                                          size_t *out_size) {
    int offset = 0;
    while (offset + 4 < size) {
        const uint32_t nal_len = ((uint32_t)data[offset] << 24) |
                                 ((uint32_t)data[offset + 1] << 16) |
                                 ((uint32_t)data[offset + 2] << 8) | (uint32_t)data[offset + 3];
        offset += 4;
        if (nal_len == 0 || offset + (int)nal_len > size) {
            return false;
        }
        /* HEVC NAL header: type is bits[1..6] of first byte. */
        const uint8_t nal_type = (data[offset] >> 1) & 0x3F;
        if (nal_type == HEVC_NAL_UNSPEC62) {
            *out_nal = data + offset;
            *out_size = nal_len;
            return true;
        }
        offset += (int)nal_len;
    }
    return false;
}

/* Annex B fallback: scan for 0x000001 / 0x00000001 start codes. */
static const uint8_t *find_next_nal(const uint8_t *data, size_t size, size_t *nal_size) {
    size_t i = 0;
    size_t header = 0;
    while (i + 2 < size) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            header = 3;
            break;
        }
        if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 &&
            data[i + 3] == 1) {
            header = 4;
            break;
        }
        i++;
    }
    if (header == 0) {
        return NULL;
    }
    i += header;
    const uint8_t *nal_start = data + i;
    const size_t remaining = size - i;
    for (size_t j = 0; j + 2 < remaining; j++) {
        if (nal_start[j] == 0 && nal_start[j + 1] == 0 &&
            (nal_start[j + 2] == 1 ||
             (j + 3 < remaining && nal_start[j + 2] == 0 && nal_start[j + 3] == 1))) {
            *nal_size = j;
            return nal_start;
        }
    }
    *nal_size = remaining;
    return nal_start;
}

static bool
find_unspec62_annex_b(const uint8_t *data, int size, const uint8_t **out_nal, size_t *out_size) {
    const uint8_t *pos = data;
    size_t remaining = (size_t)size;
    while (remaining > 2) {
        size_t nal_size = 0;
        const uint8_t *nal = find_next_nal(pos, remaining, &nal_size);
        if (nal == NULL || nal_size < 2) {
            return false;
        }
        const uint8_t nal_type = (nal[0] >> 1) & 0x3F;
        if (nal_type == HEVC_NAL_UNSPEC62) {
            *out_nal = nal;
            *out_size = nal_size;
            return true;
        }
        const size_t consumed = (size_t)(nal - pos) + nal_size;
        pos += consumed;
        remaining -= consumed;
    }
    return false;
}

/* Write one RPU payload as 4-byte big-endian length + bytes. */
static bool write_rpu_payload(FILE *fp, const DoviData *data) {
    uint8_t len_be[4];
    const uint32_t len32 = (uint32_t)data->len;
    len_be[0] = (uint8_t)((len32 >> 24) & 0xFF);
    len_be[1] = (uint8_t)((len32 >> 16) & 0xFF);
    len_be[2] = (uint8_t)((len32 >> 8) & 0xFF);
    len_be[3] = (uint8_t)(len32 & 0xFF);
    if (fwrite(len_be, 1, 4, fp) != 4) {
        return false;
    }
    if (fwrite(data->data, 1, data->len, fp) != data->len) {
        return false;
    }
    return true;
}

vmav_status_t
vmav_rpu_extract(const char *input_path, const char *output_path, vmav_rpu_extract_t *out) {
    if (input_path == NULL || output_path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_rpu_extract: null arg");
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->output_path, sizeof(out->output_path), "%s", output_path);

    struct stat st;
    if (stat(output_path, &st) == 0 && st.st_size > 0) {
        out->skipped = true;
        VMAV_LOG_INFO("rpu_extract: '%s' already exists (%lld bytes), skipping",
                      output_path,
                      (long long)st.st_size);
        return VMAV_OK_STATUS;
    }

    AVFormatContext *fmt_ctx = NULL;
    AVPacket *pkt = NULL;
    FILE *out_fp = NULL;
    vmav_ui_progress_t *prog = NULL;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    vmav_status_t status = VMAV_OK_STATUS;

    int rc = avformat_open_input(&fmt_ctx, input_path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        status = VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", input_path, errbuf);
        goto cleanup;
    }
    rc = avformat_find_stream_info(fmt_ctx, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "stream info: %s", errbuf);
        goto cleanup;
    }
    const int vidx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) {
        status = VMAV_ERR(VMAV_ERR_NOT_FOUND, "no video stream in '%s'", input_path);
        goto cleanup;
    }
    AVStream *vs = fmt_ctx->streams[vidx];
    if (vs->codecpar->codec_id != AV_CODEC_ID_HEVC) {
        status = VMAV_ERR(VMAV_ERR_BAD_ARG,
                          "video stream is not HEVC (codec: %s)",
                          avcodec_get_name(vs->codecpar->codec_id));
        goto cleanup;
    }

    out_fp = fopen(output_path, "wb");
    if (out_fp == NULL) {
        status = VMAV_ERR(VMAV_ERR_IO, "cannot create '%s'", output_path);
        goto cleanup;
    }
    pkt = av_packet_alloc();
    if (pkt == NULL) {
        status = VMAV_ERR(VMAV_ERR_NO_MEM, "av_packet_alloc");
        goto cleanup;
    }

    const int64_t total_us = fmt_ctx->duration > 0 ? fmt_ctx->duration : 0;
    if (total_us > 0) {
        prog = vmav_ui_progress_new(stderr, "rpu-extract", (uint64_t)total_us);
    }

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != vidx) {
            av_packet_unref(pkt);
            continue;
        }
        const uint8_t *nal_data = NULL;
        size_t nal_size = 0;
        bool found = find_unspec62_length_prefixed(pkt->data, pkt->size, &nal_data, &nal_size);
        if (!found) {
            found = find_unspec62_annex_b(pkt->data, pkt->size, &nal_data, &nal_size);
        }
        if (found && nal_data != NULL && nal_size > 0) {
            DoviRpuOpaque *rpu = dovi_parse_unspec62_nalu(nal_data, nal_size);
            if (rpu != NULL) {
                const char *err = dovi_rpu_get_error(rpu);
                if (err != NULL) {
                    if (out->rpu_count == 0) {
                        VMAV_LOG_WARN("rpu_extract: first RPU parse error: %s", err);
                    }
                } else {
                    const DoviData *data = dovi_write_rpu(rpu);
                    if (data != NULL && data->data != NULL && data->len > 0) {
                        if (!write_rpu_payload(out_fp, data)) {
                            dovi_data_free(data);
                            dovi_rpu_free(rpu);
                            status = VMAV_ERR(VMAV_ERR_IO, "write to '%s' failed", output_path);
                            av_packet_unref(pkt);
                            goto cleanup;
                        }
                        out->rpu_count++;
                        dovi_data_free(data);
                    }
                }
                dovi_rpu_free(rpu);
            }
        }
        if (prog != NULL && pkt->pts != AV_NOPTS_VALUE) {
            const int64_t cur_us =
                av_rescale_q(pkt->pts, vs->time_base, (AVRational){1, AV_TIME_BASE});
            if (cur_us > 0) {
                vmav_ui_progress_set(prog, (uint64_t)cur_us);
            }
        }
        av_packet_unref(pkt);
    }

    if (prog != NULL) {
        char msg[32];
        snprintf(msg, sizeof(msg), "%d RPUs", out->rpu_count);
        vmav_ui_progress_finish(prog, msg);
    }

    if (out->rpu_count == 0) {
        status = VMAV_ERR(VMAV_ERR_NOT_FOUND, "no UNSPEC62 NAL units in '%s'", input_path);
    }

cleanup:
    if (prog != NULL) {
        vmav_ui_progress_free(prog);
    }
    if (out_fp != NULL) {
        fclose(out_fp);
        if (!vmav_status_ok(status) || out->rpu_count == 0) {
            remove(output_path);
        }
    }
    av_packet_free(&pkt);
    if (fmt_ctx != NULL) {
        avformat_close_input(&fmt_ctx);
    }
    return status;
}
