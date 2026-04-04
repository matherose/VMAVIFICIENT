/**
 * Quick test for PGS-to-SRT conversion on the Dune MKV.
 * Build: (from build dir) cc -I../include -I deps/ffmpeg/include \
 *   -I /opt/homebrew/include ../test_pgs.c \
 *   src/subtitle_convert.c src/media_tracks.c src/utils.c \
 *   ... (too many deps, use cmake)
 *
 * Instead, we'll integrate into the main build and run.
 */

#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "media_tracks.h"
#include "subtitle_convert.h"
#include "utils.h"

int main(void) {
  setbuf(stdout, NULL); /* disable buffering for pipe/file output */
  init_logging();

  const char *input =
      "Dune 2021 VFF VO DOLBY TRUEHD ATMOS 2160P UHD BLURAY REMUX HEVC "
      "HDR10 DOLBY VISION-Obi.mkv";

  printf("=== PGS-to-SRT Test ===\n\n");

  /* Enumerate tracks */
  MediaTracks tracks = get_media_tracks(input);
  if (tracks.error != 0) {
    fprintf(stderr, "Error: cannot read tracks (%d)\n", tracks.error);
    return 1;
  }

  printf("Subtitle tracks (%d):\n", tracks.subtitle_count);
  for (int i = 0; i < tracks.subtitle_count; i++) {
    TrackInfo *s = &tracks.subtitles[i];
    printf("  #%d  %-6s  %-20s  PGS=%d  TEXT=%d  forced=%d  sdh=%d  %s\n",
           s->index, s->language, s->codec, is_pgs_subtitle(s),
           is_text_subtitle(s), s->is_forced, s->is_sdh, s->name);
  }

  /* Try OCR on the first PGS track (forced French — should be short) */
  for (int i = 0; i < tracks.subtitle_count; i++) {
    TrackInfo *s = &tracks.subtitles[i];
    if (!is_pgs_subtitle(s))
      continue;

    /* Only test the forced track (less subtitles = faster test) */
    if (!s->is_forced)
      continue;

    char output[256];
    snprintf(output, sizeof(output), "test_pgs_%s%s.srt", s->language,
             s->is_forced ? "_forced" : "");

    /* Remove old test output */
    remove(output);

    /* Skip the old FFmpeg probe — it never returns subtitles anyway */
    if (0) {
      AVFormatContext *probe_ctx = NULL;
      avformat_open_input(&probe_ctx, input, NULL, NULL);
      avformat_find_stream_info(probe_ctx, NULL);
      AVStream *ps = probe_ctx->streams[s->index];
      const AVCodec *pdec = avcodec_find_decoder(ps->codecpar->codec_id);
      AVCodecContext *pctx = avcodec_alloc_context3(pdec);
      avcodec_parameters_to_context(pctx, ps->codecpar);
      avcodec_open2(pctx, pdec, NULL);
      AVPacket *ppkt = av_packet_alloc();
      int probed = 0, pkt_count = 0;
      while (av_read_frame(probe_ctx, ppkt) >= 0 && probed < 3 && pkt_count < 20) {
        if (ppkt->stream_index != s->index) { av_packet_unref(ppkt); continue; }
        pkt_count++;
        AVSubtitle sub;
        int got = 0;
        avcodec_decode_subtitle2(pctx, &sub, &got, ppkt);
        printf("  pkt pts=%lld size=%d got=%d\n", (long long)ppkt->pts, ppkt->size, got);
        if (got) {
          printf("  num_rects=%u start=%u end=%u\n", sub.num_rects, sub.start_display_time, sub.end_display_time);
          for (unsigned r = 0; r < sub.num_rects; r++) {
            AVSubtitleRect *rect = sub.rects[r];
            printf("  rect[%u] type=%d x=%d y=%d w=%d h=%d nb_colors=%d\n",
                   r, rect->type, rect->x, rect->y, rect->w, rect->h, rect->nb_colors);
            printf("    data[0]=%p data[1]=%p linesize[0]=%d\n",
                   (void*)rect->data[0], (void*)rect->data[1], rect->linesize[0]);
            if (rect->data[0] && rect->w > 0) {
              printf("    first 16 bytes: ");
              int n = rect->linesize[0] < 16 ? rect->linesize[0] : 16;
              for (int j = 0; j < n; j++) printf("%02x ", rect->data[0][j]);
              printf("\n");
            }
            if (rect->data[1] && rect->nb_colors > 0) {
              uint32_t *pal = (uint32_t*)rect->data[1];
              printf("    palette[0..3]: %08x %08x %08x %08x\n",
                     pal[0], rect->nb_colors > 1 ? pal[1] : 0,
                     rect->nb_colors > 2 ? pal[2] : 0,
                     rect->nb_colors > 3 ? pal[3] : 0);
            }
          }
          avsubtitle_free(&sub);
          probed++;
        }
        av_packet_unref(ppkt);
      }
      printf("  Total probed: %d subtitle events\n\n", probed);
      av_packet_free(&ppkt);
      avcodec_free_context(&pctx);
      avformat_close_input(&probe_ctx);
    }

    printf("Converting PGS #%d (%s, %s) to SRT...\n", s->index, s->language,
           s->is_forced ? "forced" : "full");

    SubtitleConvertResult result =
        convert_pgs_to_srt(input, s, output, NULL);

    if (result.error != 0) {
      fprintf(stderr, "  ERROR: %d\n", result.error);
    } else if (result.skipped) {
      printf("  SKIPPED (already exists)\n");
    } else {
      printf("  OK: %d subtitles written to %s\n", result.subtitle_count,
             output);
    }

    /* Show first few lines of output */
    if (result.error == 0 && !result.skipped) {
      printf("\n--- First 30 lines of %s ---\n", output);
      FILE *fp = fopen(output, "r");
      if (fp) {
        char line[512];
        int n = 0;
        while (fgets(line, sizeof(line), fp) && n < 30) {
          printf("%s", line);
          n++;
        }
        fclose(fp);
      }
      printf("--- end ---\n");
    }

    break; /* Only test one track */
  }

  /* Also test track naming */
  printf("\n=== Track Naming Test ===\n");

  char name[256];
  build_audio_track_name(name, sizeof(name), "eng", 6, FRENCH_AUDIO_VFF);
  printf("  Audio eng 6ch:  %s\n", name);

  build_audio_track_name(name, sizeof(name), "fre", 6, FRENCH_AUDIO_VFF);
  printf("  Audio fre VFF:  %s\n", name);

  build_audio_track_name(name, sizeof(name), "fre", 8, FRENCH_AUDIO_VFQ);
  printf("  Audio fre VFQ:  %s\n", name);

  build_audio_track_name(name, sizeof(name), "fre", 2, FRENCH_AUDIO_VFI);
  printf("  Audio fre VFI:  %s\n", name);

  build_audio_track_name(name, sizeof(name), "ger", 2, FRENCH_AUDIO_VFF);
  printf("  Audio ger 2ch:  %s\n", name);

  build_subtitle_track_name(name, sizeof(name), "fre", 1, 1, 0);
  printf("  Sub fre forced: %s\n", name);

  build_subtitle_track_name(name, sizeof(name), "eng", 1, 0, 0);
  printf("  Sub eng full:   %s\n", name);

  build_subtitle_track_name(name, sizeof(name), "eng", 1, 0, 1);
  printf("  Sub eng sdh:    %s\n", name);

  free_media_tracks(&tracks);
  return 0;
}
