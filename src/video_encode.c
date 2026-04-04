/**
 * @file video_encode.c
 * @brief AV1 video encoding via SVT-AV1-HDR with Dolby Vision / HDR10+ support.
 *
 * Pipeline: FFmpeg decode → crop → SVT-AV1-HDR encode → FFmpeg MKV mux.
 * Dolby Vision RPU metadata is read from a .rpu.bin file and attached
 * per-frame as ITU-T T.35 metadata OBUs.
 */

#include "video_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include <EbSvtAv1.h>
#include <EbSvtAv1Enc.h>
#include <EbSvtAv1Formats.h>
#include <EbSvtAv1Metadata.h>

#include <libdovi/rpu_parser.h>

/* ====================================================================== */
/*  Suppress SVT-AV1 log output (we show our own progress)               */
/* ====================================================================== */

static void svt_silent_log(void *context, SvtAv1LogLevel level,
                           const char *tag, const char *fmt, va_list args) {
  (void)context;
  (void)level;
  (void)tag;
  (void)fmt;
  (void)args;
}

/* ====================================================================== */
/*  RPU file reader — reads length-prefixed RPU entries                   */
/* ====================================================================== */

typedef struct {
  FILE *fp;
  int eof;
} RpuReader;

static RpuReader rpu_reader_open(const char *path) {
  RpuReader r = {.fp = NULL, .eof = 1};
  if (!path || !path[0])
    return r;
  r.fp = fopen(path, "rb");
  if (r.fp)
    r.eof = 0;
  return r;
}

static void rpu_reader_close(RpuReader *r) {
  if (r->fp) {
    fclose(r->fp);
    r->fp = NULL;
  }
}

/**
 * Read the next RPU entry. Caller must free *out_data with free().
 * Returns the size or 0 on EOF/error.
 */
static size_t rpu_reader_next(RpuReader *r, uint8_t **out_data) {
  *out_data = NULL;
  if (!r->fp || r->eof)
    return 0;

  uint8_t len_be[4];
  if (fread(len_be, 1, 4, r->fp) != 4) {
    r->eof = 1;
    return 0;
  }

  uint32_t len = ((uint32_t)len_be[0] << 24) | ((uint32_t)len_be[1] << 16) |
                 ((uint32_t)len_be[2] << 8) | (uint32_t)len_be[3];

  if (len == 0 || len > 64 * 1024) {
    r->eof = 1;
    return 0;
  }

  uint8_t *buf = malloc(len);
  if (!buf) {
    r->eof = 1;
    return 0;
  }

  if (fread(buf, 1, len, r->fp) != len) {
    free(buf);
    r->eof = 1;
    return 0;
  }

  *out_data = buf;
  return len;
}

/* ====================================================================== */
/*  Progress display                                                      */
/* ====================================================================== */

static void print_progress(int64_t frames_done, int64_t total_frames,
                           time_t start_time) {
  if (total_frames <= 0)
    return;

  double pct = (double)frames_done / total_frames;
  if (pct > 1.0)
    pct = 1.0;

  int bar_width = 30;
  int filled = (int)(pct * bar_width);
  char bar[64];
  for (int i = 0; i < bar_width; i++)
    bar[i] = (i < filled) ? '=' : (i == filled) ? '>' : ' ';
  bar[bar_width] = '\0';

  time_t now = time(NULL);
  double elapsed = difftime(now, start_time);
  double fps = (elapsed > 0.5) ? frames_done / elapsed : 0;

  char eta_str[32] = "";
  if (pct > 0.01 && elapsed > 1.0) {
    double remaining = elapsed * (1.0 - pct) / pct;
    int eta_min = (int)(remaining / 60);
    int eta_sec = (int)remaining % 60;
    snprintf(eta_str, sizeof(eta_str), "ETA %02d:%02d", eta_min, eta_sec);
  }

  fprintf(stderr, "\r  [%s] %3d%%  %lld frames  %.1f fps  %s   ", bar,
          (int)(pct * 100), (long long)frames_done, fps, eta_str);
  fflush(stderr);
}

/* ====================================================================== */
/*  SVT-AV1 configuration from preset                                     */
/* ====================================================================== */

#define UNSET (-1)

static void apply_preset_to_config(EbSvtAv1EncConfiguration *cfg,
                                   const EncodePreset *p, int film_grain,
                                   int target_bitrate_kbps) {
  cfg->enc_mode = (int8_t)p->preset;
  cfg->intra_period_length = p->keyint;
  cfg->tune = (uint8_t)p->tune;
  cfg->rate_control_mode = SVT_AV1_RC_MODE_VBR;
  cfg->target_bit_rate = (uint32_t)target_bitrate_kbps * 1000;

  cfg->ac_bias = p->ac_bias;
  cfg->enable_variance_boost = true;
  cfg->variance_boost_strength = (uint8_t)p->variance_boost;
  cfg->variance_octile = (uint8_t)p->variance_octile;
  cfg->variance_boost_curve = (uint8_t)p->variance_curve;
  cfg->sharpness = (int8_t)p->sharpness;
  cfg->luminance_qp_bias = (uint8_t)p->luminance_bias;
  cfg->enable_tf = (uint8_t)p->enable_tf;
  cfg->tf_strength = (uint8_t)p->tf_strength;
  cfg->kf_tf_strength = (uint8_t)p->kf_tf_strength;
  cfg->tx_bias = (uint8_t)p->tx_bias;
  cfg->sharp_tx = (uint8_t)p->sharp_tx;
  cfg->complex_hvs = (uint8_t)p->complex_hvs;
  cfg->noise_adaptive_filtering = (uint8_t)p->noise_adaptive_filtering;
  cfg->enable_dlf_flag = (uint8_t)p->enable_dlf;
  cfg->cdef_scaling = (uint8_t)p->cdef_scaling;
  cfg->qp_scale_compress_strength = p->qp_scale_compress_strength;
  cfg->max_tx_size = (uint8_t)p->max_tx_size;
  cfg->hbd_mds = (uint8_t)p->hbd_mds;
  cfg->adaptive_film_grain = (p->adaptive_film_grain == 1);
  cfg->alt_lambda_factors = (p->alt_lambda_factors == 1);

  if (p->noise_norm_strength != UNSET)
    cfg->noise_norm_strength = (uint8_t)p->noise_norm_strength;
  if (p->chroma_qm_min != UNSET)
    cfg->min_chroma_qm_level = (uint8_t)p->chroma_qm_min;
  if (p->chroma_qm_max != UNSET)
    cfg->max_chroma_qm_level = (uint8_t)p->chroma_qm_max;
  if (p->enable_overlays != UNSET)
    cfg->enable_overlays = (p->enable_overlays == 1);

  /* Quantization matrices */
  if (p->enable_qm >= 0)
    cfg->enable_qm = (p->enable_qm == 1);
  if (p->qm_min != UNSET)
    cfg->min_qm_level = (uint8_t)p->qm_min;
  if (p->qm_max != UNSET)
    cfg->max_qm_level = (uint8_t)p->qm_max;

  /* VBR rate control limits */
  if (p->undershoot_pct != UNSET)
    cfg->under_shoot_pct = (uint32_t)p->undershoot_pct;
  if (p->overshoot_pct != UNSET)
    cfg->over_shoot_pct = (uint32_t)p->overshoot_pct;

  /* QP bounds */
  if (p->min_qp != UNSET)
    cfg->min_qp_allowed = (uint32_t)p->min_qp;
  if (p->max_qp != UNSET)
    cfg->max_qp_allowed = (uint32_t)p->max_qp;

  /* Motion field motion vectors */
  if (p->enable_mfmv != UNSET)
    cfg->enable_mfmv = (p->enable_mfmv == 1);

  /* Intra refresh type */
  if (p->irefresh_type != UNSET)
    cfg->intra_refresh_type = (int8_t)p->irefresh_type;

  /* Adaptive quantization mode */
  if (p->aq_mode != UNSET)
    cfg->aq_mode = (uint8_t)p->aq_mode;

  /* Restoration filter */
  if (p->enable_restoration != UNSET)
    cfg->enable_restoration_filtering = (p->enable_restoration == 1);

  /* Recode loop level */
  if (p->recode_loop != UNSET)
    cfg->recode_loop = (uint32_t)p->recode_loop;

  /* VBR look-ahead distance */
  if (p->look_ahead_distance != UNSET)
    cfg->look_ahead_distance = (uint32_t)p->look_ahead_distance;

  /* Dynamic GOP */
  if (p->enable_dg != UNSET)
    cfg->enable_dg = (p->enable_dg == 1);

  /* Decoder speed optimization (0 = favor quality) */
  if (p->fast_decode != UNSET)
    cfg->fast_decode = (uint8_t)p->fast_decode;

  /* Film grain from grain score analysis */
  cfg->film_grain_denoise_strength = (uint32_t)film_grain;
}

/* ====================================================================== */
/*  Color space passthrough                                               */
/* ====================================================================== */

static void copy_color_info(EbSvtAv1EncConfiguration *cfg,
                            const AVCodecParameters *codecpar) {
  cfg->color_primaries = (EbColorPrimaries)codecpar->color_primaries;
  cfg->transfer_characteristics =
      (EbTransferCharacteristics)codecpar->color_trc;
  cfg->matrix_coefficients = (EbMatrixCoefficients)codecpar->color_space;
  cfg->color_range = (codecpar->color_range == AVCOL_RANGE_JPEG)
                         ? EB_CR_FULL_RANGE
                         : EB_CR_STUDIO_RANGE;
}

/**
 * Extract and set HDR10 static metadata from stream-level codec side data.
 * Must be called BEFORE svt_av1_enc_set_parameter / svt_av1_enc_init.
 */
static void set_hdr10_metadata(EbSvtAv1EncConfiguration *cfg,
                               const AVStream *stream) {
  const AVCodecParameters *par = stream->codecpar;

  /* Mastering display from coded side data */
  const AVPacketSideData *mdcv_sd = NULL;
  const AVPacketSideData *cll_sd = NULL;
  for (int i = 0; i < par->nb_coded_side_data; i++) {
    if (par->coded_side_data[i].type ==
        AV_PKT_DATA_MASTERING_DISPLAY_METADATA)
      mdcv_sd = &par->coded_side_data[i];
    else if (par->coded_side_data[i].type ==
             AV_PKT_DATA_CONTENT_LIGHT_LEVEL)
      cll_sd = &par->coded_side_data[i];
  }

  if (mdcv_sd && mdcv_sd->size >= (int)sizeof(AVMasteringDisplayMetadata)) {
    const AVMasteringDisplayMetadata *mdcv =
        (const AVMasteringDisplayMetadata *)mdcv_sd->data;
    if (mdcv->has_primaries) {
      cfg->mastering_display.r.x =
          (uint16_t)(av_q2d(mdcv->display_primaries[0][0]) * 50000 + 0.5);
      cfg->mastering_display.r.y =
          (uint16_t)(av_q2d(mdcv->display_primaries[0][1]) * 50000 + 0.5);
      cfg->mastering_display.g.x =
          (uint16_t)(av_q2d(mdcv->display_primaries[1][0]) * 50000 + 0.5);
      cfg->mastering_display.g.y =
          (uint16_t)(av_q2d(mdcv->display_primaries[1][1]) * 50000 + 0.5);
      cfg->mastering_display.b.x =
          (uint16_t)(av_q2d(mdcv->display_primaries[2][0]) * 50000 + 0.5);
      cfg->mastering_display.b.y =
          (uint16_t)(av_q2d(mdcv->display_primaries[2][1]) * 50000 + 0.5);
      cfg->mastering_display.white_point.x =
          (uint16_t)(av_q2d(mdcv->white_point[0]) * 50000 + 0.5);
      cfg->mastering_display.white_point.y =
          (uint16_t)(av_q2d(mdcv->white_point[1]) * 50000 + 0.5);
    }
    if (mdcv->has_luminance) {
      cfg->mastering_display.max_luma =
          (uint32_t)(av_q2d(mdcv->max_luminance) * 10000 + 0.5);
      cfg->mastering_display.min_luma =
          (uint32_t)(av_q2d(mdcv->min_luminance) * 10000 + 0.5);
    }
  }

  if (cll_sd && cll_sd->size >= (int)sizeof(AVContentLightMetadata)) {
    const AVContentLightMetadata *cll =
        (const AVContentLightMetadata *)cll_sd->data;
    cfg->content_light_level.max_cll = (uint16_t)cll->MaxCLL;
    cfg->content_light_level.max_fall = (uint16_t)cll->MaxFALL;
  }
}

/* ====================================================================== */
/*  Main encode function                                                  */
/* ====================================================================== */

VideoEncodeResult encode_video(const VideoEncodeConfig *config) {
  VideoEncodeResult result = {.error = 0, .skipped = 0, .frames_encoded = 0,
                              .bytes_written = 0};

  /* Skip if output already exists. */
  struct stat st;
  if (stat(config->output_path, &st) == 0 && st.st_size > 0) {
    result.skipped = 1;
    return result;
  }

  /* State variables */
  AVFormatContext *ifmt_ctx = NULL;
  AVCodecContext *dec_ctx = NULL;
  AVFormatContext *ofmt_ctx = NULL;
  AVStream *out_stream = NULL;
  struct SwsContext *sws_ctx = NULL;
  EbComponentType *svt_handle = NULL;
  EbSvtAv1EncConfiguration svt_config;
  AVFrame *frame = NULL;
  AVFrame *cropped_frame = NULL;
  AVPacket *pkt = NULL;
  RpuReader rpu_reader = {.fp = NULL, .eof = 1};
  int video_idx = -1;
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  int ret;

  /* Computed dimensions after crop */
  int crop_top = 0, crop_bottom = 0, crop_left = 0, crop_right = 0;
  if (config->crop && config->crop->error == 0) {
    crop_top = config->crop->top;
    crop_bottom = config->crop->bottom;
    crop_left = config->crop->left;
    crop_right = config->crop->right;
  }

  int src_w = config->info->width;
  int src_h = config->info->height;
  int out_w = src_w - crop_left - crop_right;
  int out_h = src_h - crop_top - crop_bottom;

  /* Ensure dimensions are even for YUV420 */
  out_w &= ~1;
  out_h &= ~1;

  /* SVT-AV1 internally pads dimensions up to superblock alignment (up to
     64 pixels). Allocate frame buffers with this padding so the encoder
     never reads past the end. */
  int padded_h = (out_h + 63) & ~63;

  if (out_w < 64 || out_h < 64) {
    fprintf(stderr, "  Video Error: cropped dimensions %dx%d too small\n",
            out_w, out_h);
    result.error = -1;
    return result;
  }

  /* ---- Open input ---- */
  ret = avformat_open_input(&ifmt_ctx, config->input_path, NULL, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "  Video Error: cannot open '%s': %s\n",
            config->input_path, errbuf);
    result.error = ret;
    return result;
  }

  ret = avformat_find_stream_info(ifmt_ctx, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "  Video Error: cannot read streams: %s\n", errbuf);
    result.error = ret;
    goto cleanup;
  }

  /* Find video stream */
  const AVCodec *decoder = NULL;
  video_idx =
      av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  if (video_idx < 0) {
    fprintf(stderr, "  Video Error: no video stream found\n");
    result.error = -1;
    goto cleanup;
  }

  AVStream *in_stream = ifmt_ctx->streams[video_idx];

  /* Setup decoder */
  dec_ctx = avcodec_alloc_context3(decoder);
  if (!dec_ctx) {
    result.error = -1;
    goto cleanup;
  }
  avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
  dec_ctx->pkt_timebase = in_stream->time_base;

  ret = avcodec_open2(dec_ctx, decoder, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "  Video Error: cannot open decoder: %s\n", errbuf);
    result.error = ret;
    goto cleanup;
  }

  /* Always encode in 10-bit for best quality and SVT-AV1 compatibility.
     swscale handles the conversion from any source format. */
  enum AVPixelFormat target_pix_fmt = AV_PIX_FMT_YUV420P10LE;
  int bit_depth = 10;
  int bytes_per_sample = 2;

  /* Always use swscale to copy frames into our padded buffer.
     This guarantees the buffer is large enough for SVT-AV1's internal
     alignment regardless of how FFmpeg allocated the decoder frame. */
  sws_ctx = sws_getContext(out_w, out_h, dec_ctx->pix_fmt, out_w, out_h,
                           target_pix_fmt, SWS_LANCZOS, NULL, NULL, NULL);
  if (!sws_ctx) {
    fprintf(stderr, "  Video Error: cannot create swscale context\n");
    result.error = -1;
    goto cleanup;
  }
  int need_crop = (crop_top || crop_bottom || crop_left || crop_right);

  /* ---- Initialize SVT-AV1-HDR encoder ---- */
  svt_av1_set_log_callback(svt_silent_log, NULL);
  ret = svt_av1_enc_init_handle(&svt_handle, &svt_config);
  if (ret != EB_ErrorNone) {
    fprintf(stderr, "  Video Error: svt_av1_enc_init_handle failed (%d)\n",
            ret);
    result.error = -1;
    goto cleanup;
  }

  /* Set dimensions and framerate */
  svt_config.source_width = (uint32_t)out_w;
  svt_config.source_height = (uint32_t)out_h;
  svt_config.encoder_bit_depth = (uint32_t)bit_depth;
  svt_config.encoder_color_format = EB_YUV420;

  /* Frame rate from source */
  if (in_stream->avg_frame_rate.num > 0 && in_stream->avg_frame_rate.den > 0) {
    svt_config.frame_rate_numerator = (uint32_t)in_stream->avg_frame_rate.num;
    svt_config.frame_rate_denominator = (uint32_t)in_stream->avg_frame_rate.den;
  } else if (in_stream->r_frame_rate.num > 0 &&
             in_stream->r_frame_rate.den > 0) {
    svt_config.frame_rate_numerator = (uint32_t)in_stream->r_frame_rate.num;
    svt_config.frame_rate_denominator = (uint32_t)in_stream->r_frame_rate.den;
  }

  /* Color info passthrough */
  copy_color_info(&svt_config, in_stream->codecpar);

  /* HDR10 static metadata (must be set before encoder init) */
  set_hdr10_metadata(&svt_config, in_stream);

  /* Apply quality preset */
  apply_preset_to_config(&svt_config, config->preset, config->film_grain,
                         config->target_bitrate);

  /* PQ content → variance_boost_curve 3 */
  if (in_stream->codecpar->color_trc == AVCOL_TRC_SMPTE2084)
    svt_config.variance_boost_curve = 3;

  ret = svt_av1_enc_set_parameter(svt_handle, &svt_config);
  if (ret != EB_ErrorNone) {
    fprintf(stderr, "  Video Error: svt_av1_enc_set_parameter failed (%d)\n",
            ret);
    result.error = -1;
    goto cleanup;
  }

  ret = svt_av1_enc_init(svt_handle);
  if (ret != EB_ErrorNone) {
    fprintf(stderr, "  Video Error: svt_av1_enc_init failed (%d)\n", ret);
    result.error = -1;
    goto cleanup;
  }

  /* ---- Set up output MKV muxer ---- */
  ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "matroska",
                                       config->output_path);
  if (ret < 0 || !ofmt_ctx) {
    fprintf(stderr, "  Video Error: cannot create output context\n");
    result.error = -1;
    goto cleanup;
  }

  out_stream = avformat_new_stream(ofmt_ctx, NULL);
  if (!out_stream) {
    result.error = -1;
    goto cleanup;
  }

  out_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  out_stream->codecpar->codec_id = AV_CODEC_ID_AV1;
  out_stream->codecpar->width = out_w;
  out_stream->codecpar->height = out_h;
  out_stream->codecpar->format = target_pix_fmt;
  out_stream->codecpar->color_primaries = in_stream->codecpar->color_primaries;
  out_stream->codecpar->color_trc = in_stream->codecpar->color_trc;
  out_stream->codecpar->color_space = in_stream->codecpar->color_space;
  out_stream->codecpar->color_range = in_stream->codecpar->color_range;

  /* Time base: use the encoder's frame rate */
  out_stream->time_base = (AVRational){
      (int)svt_config.frame_rate_denominator,
      (int)svt_config.frame_rate_numerator};

  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
      av_make_error_string(errbuf, sizeof(errbuf), ret);
      fprintf(stderr, "  Video Error: cannot open output '%s': %s\n",
              config->output_path, errbuf);
      result.error = ret;
      goto cleanup;
    }
  }

  /* We'll write the header after getting the first encoded packet
     (to extract the sequence header for extradata). */
  int header_written = 0;

  /* ---- Open RPU reader ---- */
  if (config->rpu_path)
    rpu_reader = rpu_reader_open(config->rpu_path);

  /* ---- Allocate frames & packets ---- */
  frame = av_frame_alloc();
  cropped_frame = av_frame_alloc();
  pkt = av_packet_alloc();
  if (!frame || !cropped_frame || !pkt) {
    result.error = -1;
    goto cleanup;
  }

  /* Allocate cropped frame buffer (padded to SVT-AV1 alignment) */
  cropped_frame->format = target_pix_fmt;
  cropped_frame->width = out_w;
  cropped_frame->height = padded_h;
  ret = av_frame_get_buffer(cropped_frame, 32);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }

  /* Total frames estimate for progress */
  int64_t total_frames = 0;
  if (config->info->duration > 0 && config->info->framerate > 0)
    total_frames = (int64_t)(config->info->duration * config->info->framerate);

  time_t start_time = time(NULL);
  time_t last_progress = 0;
  int64_t frame_number = 0;
  int pic_send_done = 0;

  /* ---- Decode → Encode loop ---- */
  while (av_read_frame(ifmt_ctx, pkt) >= 0) {
    if (pkt->stream_index != video_idx) {
      av_packet_unref(pkt);
      continue;
    }

    ret = avcodec_send_packet(dec_ctx, pkt);
    av_packet_unref(pkt);
    if (ret < 0)
      continue;

    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
      /* Copy frame into padded buffer, applying crop + format conversion */
      {
        const uint8_t *src_data[4] = {NULL};
        int src_linesize[4] = {0};
        for (int p = 0; p < 4; p++) {
          src_data[p] = frame->data[p];
          src_linesize[p] = frame->linesize[p];
        }

        if (need_crop && (crop_top || crop_left)) {
          const AVPixFmtDescriptor *desc =
              av_pix_fmt_desc_get(dec_ctx->pix_fmt);
          int h_shift = desc->log2_chroma_w;
          int v_shift = desc->log2_chroma_h;
          int is_semiplanar = (desc->nb_components >= 2 &&
                               desc->comp[1].plane == desc->comp[2].plane);

          src_data[0] = frame->data[0] +
                        crop_top * frame->linesize[0] +
                        crop_left * bytes_per_sample;

          if (is_semiplanar) {
            if (frame->data[1])
              src_data[1] = frame->data[1] +
                            (crop_top >> v_shift) * frame->linesize[1] +
                            (crop_left >> h_shift) * 2 * bytes_per_sample;
          } else {
            if (frame->data[1])
              src_data[1] = frame->data[1] +
                            (crop_top >> v_shift) * frame->linesize[1] +
                            (crop_left >> h_shift) * bytes_per_sample;
            if (frame->data[2])
              src_data[2] = frame->data[2] +
                            (crop_top >> v_shift) * frame->linesize[2] +
                            (crop_left >> h_shift) * bytes_per_sample;
          }
        }

        sws_scale(sws_ctx, src_data, src_linesize, 0, out_h,
                  cropped_frame->data, cropped_frame->linesize);
      }
      AVFrame *enc_input = cropped_frame;

      /* Build SVT-AV1 input buffer */
      EbBufferHeaderType input_buf;
      memset(&input_buf, 0, sizeof(input_buf));
      input_buf.size = sizeof(EbBufferHeaderType);

      EbSvtIOFormat io_fmt;
      io_fmt.luma = enc_input->data[0];
      io_fmt.cb = enc_input->data[1];
      io_fmt.cr = enc_input->data[2];
      io_fmt.y_stride = (uint32_t)(enc_input->linesize[0] / bytes_per_sample);
      io_fmt.cb_stride = (uint32_t)(enc_input->linesize[1] / bytes_per_sample);
      io_fmt.cr_stride = (uint32_t)(enc_input->linesize[2] / bytes_per_sample);

      input_buf.p_buffer = (uint8_t *)&io_fmt;
      input_buf.n_filled_len = (uint32_t)(out_w * out_h * bytes_per_sample * 3 / 2);
      input_buf.pts = frame_number;
      input_buf.pic_type = EB_AV1_INVALID_PICTURE;
      input_buf.flags = 0;
      input_buf.metadata = NULL;

      /* Attach Dolby Vision RPU as T.35 metadata */
      uint8_t *rpu_data = NULL;
      size_t rpu_size = rpu_reader_next(&rpu_reader, &rpu_data);
      if (rpu_data && rpu_size > 0) {
        /* Parse RPU, convert to AV1 T.35 OBU, then attach */
        DoviRpuOpaque *rpu = dovi_parse_rpu(rpu_data, rpu_size);
        if (rpu) {
          const char *err = dovi_rpu_get_error(rpu);
          if (!err) {
            /* Convert RPU to profile 8.1 for AV1 */
            dovi_convert_rpu_with_mode(rpu, 2);

            const DoviData *t35 =
                dovi_write_av1_rpu_metadata_obu_t35_complete(rpu);
            if (t35 && t35->data && t35->len > 0) {
              svt_add_metadata(&input_buf, EB_AV1_METADATA_TYPE_ITUT_T35,
                               t35->data, t35->len);
              dovi_data_free(t35);
            }
          }
          dovi_rpu_free(rpu);
        }
        free(rpu_data);
      }

      /* Send frame to encoder */
      ret = svt_av1_enc_send_picture(svt_handle, &input_buf);

      /* SVT-AV1 takes ownership of metadata via send_picture — do not free */

      if (ret != EB_ErrorNone) {
        fprintf(stderr, "  Video Error: send_picture failed (%d)\n", ret);
        av_frame_unref(frame);
        result.error = -1;
        goto flush_encoder;
      }

      frame_number++;

      /* Drain available output packets (non-blocking) */
      EbBufferHeaderType *out_pkt = NULL;
      while (svt_av1_enc_get_packet(svt_handle, &out_pkt, 0) == EB_ErrorNone) {
        if (out_pkt->n_filled_len > 0) {
          /* Write MKV header before first packet */
          if (!header_written) {
            /* Copy sequence header as extradata if present */
            if (out_pkt->flags & EB_BUFFERFLAG_HAS_TD) {
              /* The first packet with TD contains the sequence header */
              out_stream->codecpar->extradata =
                  av_mallocz(out_pkt->n_filled_len +
                             AV_INPUT_BUFFER_PADDING_SIZE);
              if (out_stream->codecpar->extradata) {
                memcpy(out_stream->codecpar->extradata, out_pkt->p_buffer,
                       out_pkt->n_filled_len);
                out_stream->codecpar->extradata_size =
                    (int)out_pkt->n_filled_len;
              }
            }

            ret = avformat_write_header(ofmt_ctx, NULL);
            if (ret < 0) {
              av_make_error_string(errbuf, sizeof(errbuf), ret);
              fprintf(stderr,
                      "  Video Error: cannot write output header: %s\n",
                      errbuf);
              svt_av1_enc_release_out_buffer(&out_pkt);
              result.error = ret;
              goto cleanup;
            }
            header_written = 1;
          }

          /* Write encoded packet */
          AVPacket *out_av_pkt = av_packet_alloc();
          if (out_av_pkt) {
            out_av_pkt->data = out_pkt->p_buffer;
            out_av_pkt->size = (int)out_pkt->n_filled_len;
            out_av_pkt->pts = out_pkt->pts;
            out_av_pkt->dts = out_pkt->dts;
            out_av_pkt->stream_index = out_stream->index;

            if (out_pkt->pic_type == EB_AV1_KEY_PICTURE ||
                out_pkt->pic_type == EB_AV1_INTRA_ONLY_PICTURE)
              out_av_pkt->flags |= AV_PKT_FLAG_KEY;

            /* Rescale timestamps */
            AVRational svt_tb = {(int)svt_config.frame_rate_denominator,
                                 (int)svt_config.frame_rate_numerator};
            av_packet_rescale_ts(out_av_pkt, svt_tb, out_stream->time_base);

            av_interleaved_write_frame(ofmt_ctx, out_av_pkt);
            result.bytes_written += out_pkt->n_filled_len;
            result.frames_encoded++;

            av_packet_free(&out_av_pkt);
          }
        }

        svt_av1_enc_release_out_buffer(&out_pkt);
      }

      av_frame_unref(frame);

      /* Progress */
      time_t now = time(NULL);
      if (now != last_progress) {
        print_progress(frame_number, total_frames, start_time);
        last_progress = now;
      }
    }
  }

  /* Flush decoder */
  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, frame) == 0) {
    /* Copy frame into padded buffer, applying crop + format conversion */
    {
      const uint8_t *src_data[4] = {NULL};
      int src_linesize[4] = {0};
      for (int p = 0; p < 4; p++) {
        src_data[p] = frame->data[p];
        src_linesize[p] = frame->linesize[p];
      }
      if (need_crop && (crop_top || crop_left)) {
        const AVPixFmtDescriptor *desc =
            av_pix_fmt_desc_get(dec_ctx->pix_fmt);
        int h_shift = desc->log2_chroma_w;
        int v_shift = desc->log2_chroma_h;
        int is_semiplanar = (desc->nb_components >= 2 &&
                             desc->comp[1].plane == desc->comp[2].plane);

        src_data[0] = frame->data[0] + crop_top * frame->linesize[0] +
                      crop_left * bytes_per_sample;
        if (is_semiplanar) {
          if (frame->data[1])
            src_data[1] = frame->data[1] +
                          (crop_top >> v_shift) * frame->linesize[1] +
                          (crop_left >> h_shift) * 2 * bytes_per_sample;
        } else {
          if (frame->data[1])
            src_data[1] = frame->data[1] +
                          (crop_top >> v_shift) * frame->linesize[1] +
                          (crop_left >> h_shift) * bytes_per_sample;
          if (frame->data[2])
            src_data[2] = frame->data[2] +
                          (crop_top >> v_shift) * frame->linesize[2] +
                          (crop_left >> h_shift) * bytes_per_sample;
        }
      }
      sws_scale(sws_ctx, src_data, src_linesize, 0, out_h,
                cropped_frame->data, cropped_frame->linesize);
    }
    AVFrame *enc_input = cropped_frame;

    EbBufferHeaderType input_buf;
    memset(&input_buf, 0, sizeof(input_buf));
    input_buf.size = sizeof(EbBufferHeaderType);

    EbSvtIOFormat io_fmt;
    io_fmt.luma = enc_input->data[0];
    io_fmt.cb = enc_input->data[1];
    io_fmt.cr = enc_input->data[2];
    io_fmt.y_stride = (uint32_t)(enc_input->linesize[0] / bytes_per_sample);
    io_fmt.cb_stride = (uint32_t)(enc_input->linesize[1] / bytes_per_sample);
    io_fmt.cr_stride = (uint32_t)(enc_input->linesize[2] / bytes_per_sample);

    input_buf.p_buffer = (uint8_t *)&io_fmt;
    input_buf.n_filled_len = (uint32_t)(out_w * out_h * bytes_per_sample * 3 / 2);
    input_buf.pts = frame_number;
    input_buf.pic_type = EB_AV1_INVALID_PICTURE;

    /* RPU for flushed frames too */
    uint8_t *rpu_data = NULL;
    size_t rpu_size = rpu_reader_next(&rpu_reader, &rpu_data);
    if (rpu_data && rpu_size > 0) {
      DoviRpuOpaque *rpu = dovi_parse_rpu(rpu_data, rpu_size);
      if (rpu) {
        const char *err = dovi_rpu_get_error(rpu);
        if (!err) {
          dovi_convert_rpu_with_mode(rpu, 2);
          const DoviData *t35 =
              dovi_write_av1_rpu_metadata_obu_t35_complete(rpu);
          if (t35 && t35->data && t35->len > 0) {
            svt_add_metadata(&input_buf, EB_AV1_METADATA_TYPE_ITUT_T35,
                             t35->data, t35->len);
            dovi_data_free(t35);
          }
        }
        dovi_rpu_free(rpu);
      }
      free(rpu_data);
    }

    svt_av1_enc_send_picture(svt_handle, &input_buf);
    /* SVT-AV1 takes ownership of metadata — do not free */
    frame_number++;
    av_frame_unref(frame);
  }

flush_encoder:
  /* Signal end of stream to SVT-AV1 */
  {
    EbBufferHeaderType eos_buf;
    memset(&eos_buf, 0, sizeof(eos_buf));
    eos_buf.size = sizeof(EbBufferHeaderType);
    eos_buf.flags = EB_BUFFERFLAG_EOS;
    eos_buf.pic_type = EB_AV1_INVALID_PICTURE;
    svt_av1_enc_send_picture(svt_handle, &eos_buf);
  }
  pic_send_done = 1;

  /* Drain all remaining encoded packets */
  {
    EbBufferHeaderType *out_pkt = NULL;
    while (svt_av1_enc_get_packet(svt_handle, &out_pkt, (uint8_t)pic_send_done) ==
           EB_ErrorNone) {
      if (out_pkt->flags & EB_BUFFERFLAG_EOS) {
        svt_av1_enc_release_out_buffer(&out_pkt);
        break;
      }

      if (out_pkt->n_filled_len > 0) {
        if (!header_written) {
          if (out_pkt->flags & EB_BUFFERFLAG_HAS_TD) {
            out_stream->codecpar->extradata =
                av_mallocz(out_pkt->n_filled_len +
                           AV_INPUT_BUFFER_PADDING_SIZE);
            if (out_stream->codecpar->extradata) {
              memcpy(out_stream->codecpar->extradata, out_pkt->p_buffer,
                     out_pkt->n_filled_len);
              out_stream->codecpar->extradata_size =
                  (int)out_pkt->n_filled_len;
            }
          }
          ret = avformat_write_header(ofmt_ctx, NULL);
          if (ret < 0) {
            svt_av1_enc_release_out_buffer(&out_pkt);
            result.error = ret;
            goto cleanup;
          }
          header_written = 1;
        }

        AVPacket *out_av_pkt = av_packet_alloc();
        if (out_av_pkt) {
          out_av_pkt->data = out_pkt->p_buffer;
          out_av_pkt->size = (int)out_pkt->n_filled_len;
          out_av_pkt->pts = out_pkt->pts;
          out_av_pkt->dts = out_pkt->dts;
          out_av_pkt->stream_index = out_stream->index;

          if (out_pkt->pic_type == EB_AV1_KEY_PICTURE ||
              out_pkt->pic_type == EB_AV1_INTRA_ONLY_PICTURE)
            out_av_pkt->flags |= AV_PKT_FLAG_KEY;

          AVRational svt_tb = {(int)svt_config.frame_rate_denominator,
                               (int)svt_config.frame_rate_numerator};
          av_packet_rescale_ts(out_av_pkt, svt_tb, out_stream->time_base);

          av_interleaved_write_frame(ofmt_ctx, out_av_pkt);
          result.bytes_written += out_pkt->n_filled_len;
          result.frames_encoded++;

          av_packet_free(&out_av_pkt);
        }
      }

      svt_av1_enc_release_out_buffer(&out_pkt);
    }
  }

  /* Write MKV trailer */
  if (header_written)
    av_write_trailer(ofmt_ctx);

  /* Final progress */
  {
    time_t end_time = time(NULL);
    int elapsed = (int)difftime(end_time, start_time);
    fprintf(stderr, "\r  [");
    for (int i = 0; i < 30; i++)
      fprintf(stderr, "=");
    fprintf(stderr, "] 100%%  %lld frames  Done in %02d:%02d          \n",
            (long long)result.frames_encoded, elapsed / 60, elapsed % 60);
  }

cleanup:
  rpu_reader_close(&rpu_reader);
  av_frame_free(&frame);
  av_frame_free(&cropped_frame);
  av_packet_free(&pkt);

  if (svt_handle) {
    svt_av1_enc_deinit(svt_handle);
    svt_av1_enc_deinit_handle(svt_handle);
  }

  if (sws_ctx)
    sws_freeContext(sws_ctx);

  avcodec_free_context(&dec_ctx);

  if (ofmt_ctx) {
    if (ofmt_ctx->pb && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
      avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
  }

  if (ifmt_ctx)
    avformat_close_input(&ifmt_ctx);

  /* Remove output on failure */
  if (result.error != 0)
    remove(config->output_path);

  return result;
}
