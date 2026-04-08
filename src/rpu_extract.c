/**
 * @file rpu_extract.c
 * @brief Implementation of Dolby Vision RPU extraction from HEVC streams.
 */

#include "rpu_extract.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>

#include <libdovi/rpu_parser.h>

/** HEVC NAL unit type for Dolby Vision RPU (UNSPEC62). */
#define HEVC_NAL_UNSPEC62 62

/**
 * @brief Find the next NAL unit in an Annex B or length-prefixed byte stream.
 *
 * Scans @p data for a start code (0x000001 or 0x00000001) and returns a
 * pointer to the first byte after the start code. Sets @p nal_size to the
 * number of bytes until the next start code (or end of buffer).
 *
 * @param data      Input buffer.
 * @param size      Size of input buffer.
 * @param nal_size  [out] Size of the located NAL unit.
 * @return Pointer to the NAL unit data, or NULL if none found.
 */
static const uint8_t *find_next_nal(const uint8_t *data, size_t size,
                                    size_t *nal_size) {
  size_t i = 0;

  /* Skip to the start code. */
  while (i + 2 < size) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      i += 3;
      goto found;
    }
    if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 &&
        data[i + 3] == 1) {
      i += 4;
      goto found;
    }
    i++;
  }
  return NULL;

found:;
  const uint8_t *nal_start = data + i;
  size_t remaining = size - i;

  /* Find the next start code to determine NAL size. */
  for (size_t j = 0; j + 2 < remaining; j++) {
    if (nal_start[j] == 0 && nal_start[j + 1] == 0 &&
        (nal_start[j + 2] == 1 || (j + 3 < remaining && nal_start[j + 2] == 0 &&
                                   nal_start[j + 3] == 1))) {
      *nal_size = j;
      return nal_start;
    }
  }

  /* Last NAL in the buffer. */
  *nal_size = remaining;
  return nal_start;
}

/**
 * @brief Extract UNSPEC62 NAL units from a packet using length-prefixed format.
 *
 * FFmpeg demuxes HEVC into 4-byte length-prefixed NAL units (mp4/mkv style).
 * This function iterates through them and looks for NAL type 62.
 *
 * @param pkt_data   Packet data buffer.
 * @param pkt_size   Packet data size.
 * @param out_nal    [out] Pointer to the UNSPEC62 NAL data (not owned).
 * @param out_size   [out] Size of the UNSPEC62 NAL data.
 * @return 1 if an UNSPEC62 NAL was found, 0 otherwise.
 */
static int find_unspec62_length_prefixed(const uint8_t *pkt_data, int pkt_size,
                                         const uint8_t **out_nal,
                                         size_t *out_size) {
  int offset = 0;
  while (offset + 4 < pkt_size) {
    uint32_t nal_len = ((uint32_t)pkt_data[offset] << 24) |
                       ((uint32_t)pkt_data[offset + 1] << 16) |
                       ((uint32_t)pkt_data[offset + 2] << 8) |
                       (uint32_t)pkt_data[offset + 3];
    offset += 4;

    if (nal_len == 0 || offset + (int)nal_len > pkt_size)
      break;

    /* HEVC NAL header: first byte contains forbidden_zero_bit(1) +
       nal_unit_type(6) + nuh_layer_id(6 high bits). Type is bits [1..6]. */
    uint8_t nal_type = (pkt_data[offset] >> 1) & 0x3F;

    if (nal_type == HEVC_NAL_UNSPEC62) {
      *out_nal = pkt_data + offset;
      *out_size = nal_len;
      return 1;
    }

    offset += (int)nal_len;
  }
  return 0;
}

/**
 * @brief Try Annex B start-code based search for UNSPEC62 NAL units.
 */
static int find_unspec62_annex_b(const uint8_t *pkt_data, int pkt_size,
                                 const uint8_t **out_nal, size_t *out_size) {
  const uint8_t *pos = pkt_data;
  size_t remaining = (size_t)pkt_size;

  while (remaining > 2) {
    size_t nal_size = 0;
    const uint8_t *nal = find_next_nal(pos, remaining, &nal_size);
    if (!nal || nal_size < 2)
      break;

    uint8_t nal_type = (nal[0] >> 1) & 0x3F;
    if (nal_type == HEVC_NAL_UNSPEC62) {
      *out_nal = nal;
      *out_size = nal_size;
      return 1;
    }

    size_t consumed = (size_t)(nal - pos) + nal_size;
    pos += consumed;
    remaining -= consumed;
  }
  return 0;
}

/**
 * @brief Print a progress bar for RPU extraction.
 */
static void print_extract_progress(int64_t current_pts, int64_t total_duration,
                                   int rpu_count, time_t start_time) {
  if (total_duration <= 0)
    return;

  double pct = (double)current_pts / total_duration;
  if (pct > 1.0)
    pct = 1.0;

  int bar_width = 30;
  int filled = (int)(pct * bar_width);

  char bar[64];
  for (int i = 0; i < bar_width; i++) {
    if (i < filled)
      bar[i] = '=';
    else if (i == filled)
      bar[i] = '>';
    else
      bar[i] = ' ';
  }
  bar[bar_width] = '\0';

  time_t now = time(NULL);
  double elapsed = difftime(now, start_time);

  char eta_str[32] = "";
  if (pct > 0.01 && elapsed > 1.0) {
    double remaining = elapsed * (1.0 - pct) / pct;
    int eta_min = (int)(remaining / 60);
    int eta_sec = (int)remaining % 60;
    snprintf(eta_str, sizeof(eta_str), "ETA %02d:%02d", eta_min, eta_sec);
  }

  fprintf(stderr, "\r  [%s] %3d%%  %d RPUs  %s   ", bar, (int)(pct * 100),
          rpu_count, eta_str);
  fflush(stderr);
}

void build_rpu_filename(char *buf, size_t bufsize, const char *base_name) {
  snprintf(buf, bufsize, "%s.rpu.bin", base_name);
}

RpuExtractResult extract_rpu(const char *input_path, const char *output_path) {
  RpuExtractResult result = {.error = 0, .skipped = 0, .rpu_count = 0};
  snprintf(result.output_path, sizeof(result.output_path), "%s", output_path);

  /* Skip if output already exists. */
  struct stat st;
  if (stat(output_path, &st) == 0 && st.st_size > 0) {
    result.skipped = 1;
    return result;
  }

  AVFormatContext *fmt_ctx = NULL;
  AVPacket *pkt = NULL;
  FILE *out_fp = NULL;
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  int ret;

  /* Open input. */
  ret = avformat_open_input(&fmt_ctx, input_path, NULL, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "  RPU Error: cannot open '%s': %s\n", input_path, errbuf);
    result.error = ret;
    return result;
  }

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) {
    av_make_error_string(errbuf, sizeof(errbuf), ret);
    fprintf(stderr, "  RPU Error: cannot read streams: %s\n", errbuf);
    result.error = ret;
    goto cleanup;
  }

  /* Find the video stream. */
  int video_idx =
      av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (video_idx < 0) {
    fprintf(stderr, "  RPU Error: no video stream found\n");
    result.error = -1;
    goto cleanup;
  }

  AVStream *video_stream = fmt_ctx->streams[video_idx];

  /* Verify codec is HEVC. */
  if (video_stream->codecpar->codec_id != AV_CODEC_ID_HEVC) {
    fprintf(stderr, "  RPU Error: video stream is not HEVC (codec: %s)\n",
            avcodec_get_name(video_stream->codecpar->codec_id));
    result.error = -1;
    goto cleanup;
  }

  /* Open output file. */
  out_fp = fopen(output_path, "wb");
  if (!out_fp) {
    fprintf(stderr, "  RPU Error: cannot create '%s'\n", output_path);
    result.error = -1;
    goto cleanup;
  }

  pkt = av_packet_alloc();
  if (!pkt) {
    result.error = -1;
    goto cleanup;
  }

  /* Total duration for progress. */
  int64_t total_duration = fmt_ctx->duration > 0 ? fmt_ctx->duration : 0;
  time_t start_time = time(NULL);
  time_t last_progress = 0;

  /* Iterate through all packets in the video stream. */
  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index != video_idx) {
      av_packet_unref(pkt);
      continue;
    }

    const uint8_t *nal_data = NULL;
    size_t nal_size = 0;

    /* Try length-prefixed first (common for mp4/mkv), then Annex B. */
    int found = find_unspec62_length_prefixed(pkt->data, pkt->size, &nal_data,
                                              &nal_size);
    if (!found)
      found = find_unspec62_annex_b(pkt->data, pkt->size, &nal_data, &nal_size);

    if (found && nal_data && nal_size > 0) {
      /* Parse with libdovi. */
      DoviRpuOpaque *rpu = dovi_parse_unspec62_nalu(nal_data, nal_size);
      if (rpu) {
        const char *err = dovi_rpu_get_error(rpu);
        if (err) {
          /* Log but continue -- some RPUs may be recoverable. */
          if (result.rpu_count == 0)
            fprintf(stderr, "  RPU Warning: parse error on first RPU: %s\n",
                    err);
        } else {
          /* Write the raw RPU bytes. */
          const DoviData *data = dovi_write_rpu(rpu);
          if (data && data->data && data->len > 0) {
            /* Length-prefixed format: 4-byte big-endian length + RPU data. */
            uint8_t len_be[4];
            uint32_t len32 = (uint32_t)data->len;
            len_be[0] = (len32 >> 24) & 0xFF;
            len_be[1] = (len32 >> 16) & 0xFF;
            len_be[2] = (len32 >> 8) & 0xFF;
            len_be[3] = len32 & 0xFF;

            fwrite(len_be, 1, 4, out_fp);
            fwrite(data->data, 1, data->len, out_fp);
            result.rpu_count++;

            dovi_data_free(data);
          }
        }
        dovi_rpu_free(rpu);
      }
    }

    /* Progress update. */
    time_t now = time(NULL);
    if (now != last_progress && total_duration > 0) {
      int64_t current_us = av_rescale_q(pkt->pts, video_stream->time_base,
                                        (AVRational){1, AV_TIME_BASE});
      print_extract_progress(current_us, total_duration, result.rpu_count,
                             start_time);
      last_progress = now;
    }

    av_packet_unref(pkt);
  }

  /* Final progress. */
  if (total_duration > 0) {
    print_extract_progress(total_duration, total_duration, result.rpu_count,
                           start_time);
    time_t end_time = time(NULL);
    int elapsed = (int)difftime(end_time, start_time);
    fprintf(stderr, "\r  [");
    for (int i = 0; i < 30; i++)
      fprintf(stderr, "=");
    fprintf(stderr, "] 100%%  %d RPUs  Done in %02d:%02d          \n",
            result.rpu_count, elapsed / 60, elapsed % 60);
  }

  if (result.rpu_count == 0) {
    fprintf(stderr, "  RPU Warning: no UNSPEC62 NAL units found\n");
    result.error = -1;
  }

cleanup:
  if (out_fp) {
    fclose(out_fp);
    /* Remove output on failure or if empty. */
    if (result.error != 0 || result.rpu_count == 0)
      remove(output_path);
  }
  av_packet_free(&pkt);
  if (fmt_ctx)
    avformat_close_input(&fmt_ctx);

  return result;
}
