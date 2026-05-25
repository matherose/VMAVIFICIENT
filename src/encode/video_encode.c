/* Final video encode driver.
 *
 * Demuxes the source with lavf, decodes each reference frame with
 * lavc, pushes the frames into encoder_svtav1, drains the encoded AV1
 * packets, and writes them to an IVF container the final_mux step
 * consumes.
 *
 * IVF is the simplest AV1 container that mkvmerge/ffmpeg both understand:
 *   * 32-byte header
 *   * Per-frame: 12-byte header (size + pts) + raw AV1 OBUs */

#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_svtav1.h"
#include "vmavificient/vmav_ui.h"
#include "vmavificient/vmav_video_encode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#include <sys/stat.h>

/* ====================================================================== */
/*  IVF writer                                                            */
/* ====================================================================== */

static void ivf_write_u32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)((v >> 16) & 0xFF);
    dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void ivf_write_u16(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void ivf_write_u64(uint8_t *dst, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        dst[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
}

/* IVF file header (32 bytes). nframes is patched in after the encode
 * loop completes via a seek to offset 24 + write_u32. */
static vmav_status_t ivf_write_header(FILE *fp, int width, int height, int fps_num, int fps_den) {
    uint8_t hdr[32] = {0};
    memcpy(hdr, "DKIF", 4);
    ivf_write_u16(hdr + 4, 0);  /* version */
    ivf_write_u16(hdr + 6, 32); /* header length */
    memcpy(hdr + 8, "AV01", 4); /* codec FourCC */
    ivf_write_u16(hdr + 12, (uint16_t)width);
    ivf_write_u16(hdr + 14, (uint16_t)height);
    ivf_write_u32(hdr + 16, (uint32_t)fps_num);
    ivf_write_u32(hdr + 20, (uint32_t)fps_den);
    /* nframes (offset 24, 4 bytes): patched after encode loop. */
    ivf_write_u32(hdr + 24, 0);
    /* unused (offset 28, 4 bytes). */
    if (fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return VMAV_ERR(VMAV_ERR_IO, "ivf: write header");
    }
    return VMAV_OK_STATUS;
}

static vmav_status_t ivf_write_frame(FILE *fp, const uint8_t *data, size_t size, int64_t pts) {
    uint8_t hdr[12];
    ivf_write_u32(hdr, (uint32_t)size);
    ivf_write_u64(hdr + 4, (uint64_t)pts);
    if (fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        return VMAV_ERR(VMAV_ERR_IO, "ivf: write frame header");
    }
    if (fwrite(data, 1, size, fp) != size) {
        return VMAV_ERR(VMAV_ERR_IO, "ivf: write frame payload");
    }
    return VMAV_OK_STATUS;
}

static vmav_status_t ivf_patch_nframes(FILE *fp, uint32_t nframes) {
    if (fseek(fp, 24, SEEK_SET) != 0) {
        return VMAV_ERR(VMAV_ERR_IO, "ivf: seek to patch nframes");
    }
    uint8_t buf[4];
    ivf_write_u32(buf, nframes);
    if (fwrite(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
        return VMAV_ERR(VMAV_ERR_IO, "ivf: patch nframes");
    }
    return VMAV_OK_STATUS;
}

/* ====================================================================== */
/*  HDR metadata pull from source                                         */
/* ====================================================================== */

static void fill_hdr_from_codecpar(vmav_svtav1_spec_t *spec, const AVCodecParameters *par) {
    spec->color_primaries = par->color_primaries;
    spec->color_trc = par->color_trc;
    spec->color_space = par->color_space;
    spec->color_range = par->color_range;

    for (int i = 0; i < par->nb_coded_side_data; i++) {
        const AVPacketSideData *sd = &par->coded_side_data[i];
        if (sd->type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA &&
            sd->size >= (int)sizeof(AVMasteringDisplayMetadata)) {
            /* memcpy via local for alignment safety — sd->data is a
             * uint8_t* and FFmpeg's structs require natural alignment. */
            AVMasteringDisplayMetadata m;
            memcpy(&m, sd->data, sizeof(m));
            if (m.has_primaries) {
                spec->has_mastering = true;
                spec->mastering_red_x = av_q2d(m.display_primaries[0][0]);
                spec->mastering_red_y = av_q2d(m.display_primaries[0][1]);
                spec->mastering_green_x = av_q2d(m.display_primaries[1][0]);
                spec->mastering_green_y = av_q2d(m.display_primaries[1][1]);
                spec->mastering_blue_x = av_q2d(m.display_primaries[2][0]);
                spec->mastering_blue_y = av_q2d(m.display_primaries[2][1]);
                spec->mastering_white_x = av_q2d(m.white_point[0]);
                spec->mastering_white_y = av_q2d(m.white_point[1]);
            }
            if (m.has_luminance) {
                spec->has_mastering = true;
                spec->mastering_max_luma = av_q2d(m.max_luminance);
                spec->mastering_min_luma = av_q2d(m.min_luminance);
            }
        } else if (sd->type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL &&
                   sd->size >= (int)sizeof(AVContentLightMetadata)) {
            AVContentLightMetadata cll;
            memcpy(&cll, sd->data, sizeof(cll));
            spec->has_cll = true;
            spec->cll_max_cll = (uint16_t)cll.MaxCLL;
            spec->cll_max_fall = (uint16_t)cll.MaxFALL;
        }
    }
}

/* ====================================================================== */
/*  Main encode driver                                                    */
/* ====================================================================== */

vmav_status_t vmav_video_encode(const vmav_video_encode_spec_t *spec,
                                vmav_video_encode_result_t *out) {
    if (spec == NULL || out == NULL || spec->input_path == NULL || spec->output_path == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_video_encode: null arg");
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->output_path, sizeof(out->output_path), "%s", spec->output_path);

    struct stat st;
    if (stat(spec->output_path, &st) == 0 && st.st_size > 0) {
        out->skipped = true;
        out->bytes_written = st.st_size;
        VMAV_LOG_INFO("video_encode: '%s' already exists (%lld bytes), skipping",
                      spec->output_path,
                      (long long)st.st_size);
        return VMAV_OK_STATUS;
    }

    AVFormatContext *ifmt = NULL;
    AVCodecContext *dec = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    AVFrame *scaled = NULL; /* libswscale scratch frame, only when scaling */
    struct SwsContext *sws = NULL;
    FILE *fp = NULL;
    vmav_svtav1_encoder_t *enc = NULL;
    vmav_ui_progress_t *prog = NULL;
    vmav_status_t status = VMAV_OK_STATUS;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    int rc;

    rc = avformat_open_input(&ifmt, spec->input_path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        return VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", spec->input_path, errbuf);
    }
    rc = avformat_find_stream_info(ifmt, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        avformat_close_input(&ifmt);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "stream info: %s", errbuf);
    }
    const int vidx = av_find_best_stream(ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) {
        avformat_close_input(&ifmt);
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "no video stream in '%s'", spec->input_path);
    }
    AVStream *vs = ifmt->streams[vidx];
    const AVCodec *decoder = avcodec_find_decoder(vs->codecpar->codec_id);
    if (decoder == NULL) {
        avformat_close_input(&ifmt);
        return VMAV_ERR(
            VMAV_ERR_FFMPEG, "no decoder for codec %s", avcodec_get_name(vs->codecpar->codec_id));
    }
    dec = avcodec_alloc_context3(decoder);
    if (dec == NULL || avcodec_parameters_to_context(dec, vs->codecpar) < 0 ||
        avcodec_open2(dec, decoder, NULL) < 0) {
        if (dec != NULL) {
            avcodec_free_context(&dec);
        }
        avformat_close_input(&ifmt);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "decoder open failed");
    }

    AVRational fps = av_guess_frame_rate(ifmt, vs, NULL);
    if (fps.num == 0) {
        fps = (AVRational){24000, 1001};
    }

    /* If scaling requested, allocate a SwsContext + scratch AVFrame at
     * the target dims. SVT sees the scaled dims; the decode loop pumps
     * each source frame through sws_scale into `scaled` before send.
     * Lanczos matches v1's `scale=…:flags=lanczos` filter; we keep the
     * source pix_fmt (YUV420P10LE survives the scale untouched for HDR
     * passthrough). */
    const bool do_scale = (spec->scale_width > 0 && spec->scale_height > 0);
    const int enc_width = do_scale ? spec->scale_width : dec->width;
    const int enc_height = do_scale ? spec->scale_height : dec->height;
    if (do_scale) {
        sws = sws_getContext(dec->width,
                             dec->height,
                             dec->pix_fmt,
                             enc_width,
                             enc_height,
                             dec->pix_fmt,
                             SWS_LANCZOS,
                             NULL,
                             NULL,
                             NULL);
        if (sws == NULL) {
            status = VMAV_ERR(VMAV_ERR_FFMPEG,
                              "sws_getContext failed (%dx%d → %dx%d, fmt=%d)",
                              dec->width,
                              dec->height,
                              enc_width,
                              enc_height,
                              dec->pix_fmt);
            goto cleanup;
        }
        scaled = av_frame_alloc();
        if (scaled == NULL) {
            status = VMAV_ERR(VMAV_ERR_NO_MEM, "av_frame_alloc (scaled)");
            goto cleanup;
        }
        scaled->format = dec->pix_fmt;
        scaled->width = enc_width;
        scaled->height = enc_height;
        if (av_frame_get_buffer(scaled, 0) < 0) {
            status = VMAV_ERR(VMAV_ERR_NO_MEM, "av_frame_get_buffer (scaled)");
            goto cleanup;
        }
        VMAV_LOG_INFO("video_encode: scaling %dx%d → %dx%d (lanczos)",
                      dec->width,
                      dec->height,
                      enc_width,
                      enc_height);
    }

    /* Configure the SVT-AV1 encoder spec. */
    vmav_svtav1_spec_t svt = {
        .preset = spec->preset,
        .width = enc_width,
        .height = enc_height,
        .bit_depth =
            (dec->pix_fmt == AV_PIX_FMT_YUV420P10LE || dec->pix_fmt == AV_PIX_FMT_YUV420P10BE) ? 10
                                                                                               : 8,
        .fps_num = fps.num,
        .fps_den = fps.den,
        .film_grain = spec->film_grain,
        .target_bitrate_kbps = spec->target_bitrate_kbps,
        .crf = spec->crf,
    };
    fill_hdr_from_codecpar(&svt, vs->codecpar);
    status = vmav_svtav1_encoder_open(&svt, &enc);
    if (!vmav_status_ok(status)) {
        goto cleanup;
    }

    fp = fopen(spec->output_path, "wb");
    if (fp == NULL) {
        status = VMAV_ERR(VMAV_ERR_IO, "create '%s'", spec->output_path);
        goto cleanup;
    }
    status = ivf_write_header(fp, dec->width, dec->height, fps.num, fps.den);
    if (!vmav_status_ok(status)) {
        goto cleanup;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (pkt == NULL || frame == NULL) {
        status = VMAV_ERR(VMAV_ERR_NO_MEM, "av_packet/av_frame alloc");
        goto cleanup;
    }

    /* Progress. duration in microseconds; convert to display frames if known. */
    if (vs->nb_frames > 0) {
        prog = vmav_ui_progress_new(stderr, "encode", (uint64_t)vs->nb_frames);
    } else if (ifmt->duration > 0) {
        const uint64_t est_frames = (uint64_t)((double)ifmt->duration / AV_TIME_BASE * av_q2d(fps));
        prog = vmav_ui_progress_new(stderr, "encode", est_frames);
    }

    int64_t enc_pts = 0;
    uint32_t nframes_written = 0;

    /* Helper: drain encoder packets and write to IVF. Returns OK when
     * AGAIN, EOF, or any packets present; status on real error. */
    /* (inline below to keep ownership semantics local) */

    while (av_read_frame(ifmt, pkt) >= 0) {
        if (pkt->stream_index != vidx) {
            av_packet_unref(pkt);
            continue;
        }
        rc = avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            av_make_error_string(errbuf, sizeof(errbuf), rc);
            status = VMAV_ERR(VMAV_ERR_FFMPEG, "decode: %s", errbuf);
            goto cleanup;
        }
        while (avcodec_receive_frame(dec, frame) == 0) {
            const AVFrame *src = frame;
            if (do_scale) {
                rc = sws_scale(sws,
                               (const uint8_t *const *)frame->data,
                               frame->linesize,
                               0,
                               dec->height,
                               scaled->data,
                               scaled->linesize);
                if (rc <= 0) {
                    av_frame_unref(frame);
                    status = VMAV_ERR(VMAV_ERR_FFMPEG, "sws_scale returned %d", rc);
                    goto cleanup;
                }
                src = scaled;
            }
            const uint8_t *planes[3] = {src->data[0], src->data[1], src->data[2]};
            const int strides[3] = {src->linesize[0], src->linesize[1], src->linesize[2]};
            status = vmav_svtav1_encoder_send(enc, planes, strides, enc_pts, /*eos=*/false);
            av_frame_unref(frame);
            if (!vmav_status_ok(status)) {
                goto cleanup;
            }
            enc_pts++;

            /* Drain whatever the encoder has buffered. */
            for (;;) {
                const uint8_t *data = NULL;
                size_t size = 0;
                int64_t out_pts = 0;
                bool is_key = false;
                vmav_status_t rs = vmav_svtav1_encoder_recv(enc, &data, &size, &out_pts, &is_key);
                if (!vmav_status_ok(rs)) {
                    if (rs.code == VMAV_ERR_AGAIN) {
                        break;
                    }
                    if (rs.code == VMAV_ERR_EOF) {
                        goto encode_done;
                    }
                    status = rs;
                    goto cleanup;
                }
                status = ivf_write_frame(fp, data, size, out_pts);
                vmav_svtav1_encoder_release(enc);
                if (!vmav_status_ok(status)) {
                    goto cleanup;
                }
                nframes_written++;
                if (prog != NULL) {
                    vmav_ui_progress_set(prog, nframes_written);
                }
            }
        }
    }

    /* Signal EOS to encoder. */
    status = vmav_svtav1_encoder_send(enc, NULL, NULL, 0, /*eos=*/true);
    if (!vmav_status_ok(status)) {
        goto cleanup;
    }
    for (;;) {
        const uint8_t *data = NULL;
        size_t size = 0;
        int64_t out_pts = 0;
        bool is_key = false;
        vmav_status_t rs = vmav_svtav1_encoder_recv(enc, &data, &size, &out_pts, &is_key);
        if (!vmav_status_ok(rs)) {
            if (rs.code == VMAV_ERR_AGAIN) {
                continue;
            }
            if (rs.code == VMAV_ERR_EOF) {
                break;
            }
            status = rs;
            goto cleanup;
        }
        status = ivf_write_frame(fp, data, size, out_pts);
        vmav_svtav1_encoder_release(enc);
        if (!vmav_status_ok(status)) {
            goto cleanup;
        }
        nframes_written++;
        if (prog != NULL) {
            vmav_ui_progress_set(prog, nframes_written);
        }
    }

encode_done:
    /* Patch IVF header with the actual frame count. */
    status = ivf_patch_nframes(fp, nframes_written);
    if (vmav_status_ok(status)) {
        out->frames_encoded = nframes_written;
    }
    if (prog != NULL) {
        char msg[32];
        snprintf(msg, sizeof(msg), "%u frames", nframes_written);
        vmav_ui_progress_finish(prog, msg);
    }

cleanup:
    if (prog != NULL) {
        vmav_ui_progress_free(prog);
    }
    av_frame_free(&frame);
    av_frame_free(&scaled);
    if (sws != NULL) {
        sws_freeContext(sws);
    }
    av_packet_free(&pkt);
    if (fp != NULL) {
        fclose(fp);
        if (vmav_status_ok(status)) {
            struct stat st2;
            if (stat(spec->output_path, &st2) == 0) {
                out->bytes_written = st2.st_size;
            }
        } else {
            remove(spec->output_path);
        }
    }
    if (enc != NULL) {
        vmav_svtav1_encoder_close(enc);
    }
    if (dec != NULL) {
        avcodec_free_context(&dec);
    }
    if (ifmt != NULL) {
        avformat_close_input(&ifmt);
    }
    return status;
}
