/**
 * @file media_analysis.c
 * @brief Video grain analysis via grav1synth diff.
 *
 * For each of a handful of positions through the film, extracts a short
 * lossless sample plus an hqdn3d-denoised copy, invokes grav1synth's "diff"
 * command to produce an AV1 film-grain table, and parses the per-scene Y
 * scaling points.  The reported score is the max across windows — we want
 * the grainiest passage to drive film_grain_denoise_strength, not the mean,
 * and sampling multiple windows avoids bias from whatever happens to sit at
 * the film's midpoint.
 */

#include "media_analysis.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

#ifndef VMAV_GRAV1SYNTH_BIN
#error "VMAV_GRAV1SYNTH_BIN must be defined by the build system"
#endif

extern char **environ;

/** Duration (seconds) of each sample window. */
#define SAMPLE_DURATION_SEC 15
/** Number of windows sampled across the film. */
#define NUM_SAMPLES 4
/** Centered crop dimensions applied to the sample to bound temp-file size. */
#define CROP_W 1920
#define CROP_H 1080
/** Max bytes of the grain-table file we'll parse (safety bound). */
#define MAX_TABLE_BYTES (4 * 1024 * 1024)

/** Positions (0..1) through the file at which to extract windows. */
static const double SAMPLE_POSITIONS[NUM_SAMPLES] = {0.20, 0.40, 0.60, 0.80};

/* ------------------------------------------------------------------------- */
/*  Sample-extraction pipeline                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
  AVFormatContext *fmt;
  AVCodecContext *enc;
  AVStream *stream;
} OutCtx;

/**
 * Open a single FFV1-in-MKV output context matching a decoded filter frame.
 */
static int out_open(const char *path, AVFrame *ref, AVRational time_base,
                    OutCtx *o) {
  int ret = avformat_alloc_output_context2(&o->fmt, NULL, "matroska", path);
  if (ret < 0)
    return ret;

  const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_FFV1);
  if (!enc)
    return AVERROR_ENCODER_NOT_FOUND;

  o->stream = avformat_new_stream(o->fmt, enc);
  if (!o->stream)
    return AVERROR(ENOMEM);

  o->enc = avcodec_alloc_context3(enc);
  if (!o->enc)
    return AVERROR(ENOMEM);

  o->enc->width = ref->width;
  o->enc->height = ref->height;
  o->enc->pix_fmt = ref->format;
  o->enc->time_base = time_base;
  o->enc->color_range = ref->color_range;
  o->enc->colorspace = ref->colorspace;
  o->enc->color_primaries = ref->color_primaries;
  o->enc->color_trc = ref->color_trc;
  /* level 3 = modern FFV1 with slicing; required for multi-threaded decode. */
  av_opt_set_int(o->enc->priv_data, "level", 3, 0);

  if (o->fmt->oformat->flags & AVFMT_GLOBALHEADER)
    o->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  ret = avcodec_open2(o->enc, enc, NULL);
  if (ret < 0)
    return ret;

  ret = avcodec_parameters_from_context(o->stream->codecpar, o->enc);
  if (ret < 0)
    return ret;
  o->stream->time_base = time_base;

  ret = avio_open(&o->fmt->pb, path, AVIO_FLAG_WRITE);
  if (ret < 0)
    return ret;

  return avformat_write_header(o->fmt, NULL);
}

static void out_close(OutCtx *o) {
  if (o->enc) {
    avcodec_send_frame(o->enc, NULL);
    AVPacket *pkt = av_packet_alloc();
    while (pkt && avcodec_receive_packet(o->enc, pkt) == 0) {
      pkt->stream_index = o->stream->index;
      av_packet_rescale_ts(pkt, o->enc->time_base, o->stream->time_base);
      av_interleaved_write_frame(o->fmt, pkt);
      av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
  }
  if (o->fmt) {
    if (o->fmt->pb)
      av_write_trailer(o->fmt);
    if (o->fmt->pb)
      avio_closep(&o->fmt->pb);
    avformat_free_context(o->fmt);
    o->fmt = NULL;
  }
  avcodec_free_context(&o->enc);
  o->stream = NULL;
}

/** Encode one filtered frame to one output. */
static int write_frame(OutCtx *o, AVFrame *f) {
  int ret = avcodec_send_frame(o->enc, f);
  if (ret < 0)
    return ret;
  AVPacket *pkt = av_packet_alloc();
  if (!pkt)
    return AVERROR(ENOMEM);
  while ((ret = avcodec_receive_packet(o->enc, pkt)) == 0) {
    pkt->stream_index = o->stream->index;
    av_packet_rescale_ts(pkt, o->enc->time_base, o->stream->time_base);
    ret = av_interleaved_write_frame(o->fmt, pkt);
    av_packet_unref(pkt);
    if (ret < 0)
      break;
  }
  av_packet_free(&pkt);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    ret = 0;
  return ret;
}

/**
 * Build the filter graph: buffer -> crop -> split -> [a]null / [b]hqdn3d.
 * The two sinks expose the source-cropped and denoised-cropped streams.
 */
static int build_graph(AVFilterGraph **graph, AVFilterContext **src_ctx,
                       AVFilterContext **sink_a, AVFilterContext **sink_b,
                       AVCodecContext *dec) {
  *graph = avfilter_graph_alloc();
  if (!*graph)
    return AVERROR(ENOMEM);

  char args[512];
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d"
           ":colorspace=%d:range=%d",
           dec->width, dec->height, dec->pix_fmt, dec->pkt_timebase.num,
           dec->pkt_timebase.den ? dec->pkt_timebase.den : 1,
           dec->sample_aspect_ratio.num ? dec->sample_aspect_ratio.num : 1,
           dec->sample_aspect_ratio.den ? dec->sample_aspect_ratio.den : 1,
           dec->colorspace, dec->color_range);

  int ret = avfilter_graph_create_filter(src_ctx, avfilter_get_by_name("buffer"),
                                         "in", args, NULL, *graph);
  if (ret < 0)
    return ret;

  ret = avfilter_graph_create_filter(sink_a, avfilter_get_by_name("buffersink"),
                                     "out_a", NULL, NULL, *graph);
  if (ret < 0)
    return ret;
  ret = avfilter_graph_create_filter(sink_b, avfilter_get_by_name("buffersink"),
                                     "out_b", NULL, NULL, *graph);
  if (ret < 0)
    return ret;

  int cw = dec->width < CROP_W ? dec->width : CROP_W;
  int ch = dec->height < CROP_H ? dec->height : CROP_H;

  AVFilterInOut *inputs = avfilter_inout_alloc();
  AVFilterInOut *input_b = avfilter_inout_alloc();
  AVFilterInOut *outputs = avfilter_inout_alloc();
  if (!inputs || !input_b || !outputs) {
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&input_b);
    avfilter_inout_free(&outputs);
    return AVERROR(ENOMEM);
  }

  outputs->name = av_strdup("in");
  outputs->filter_ctx = *src_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = av_strdup("out_a");
  inputs->filter_ctx = *sink_a;
  inputs->pad_idx = 0;
  inputs->next = input_b;

  input_b->name = av_strdup("out_b");
  input_b->filter_ctx = *sink_b;
  input_b->pad_idx = 0;
  input_b->next = NULL;

  char spec[512];
  /* hqdn3d with mild spatial + strong temporal (ls:cs:lt:ct).
   * Temporal denoise is what isolates grain (temporally uncorrelated), while
   * low spatial values preserve image detail so the diff fed to grav1synth
   * reflects real grain rather than destroyed content. */
  snprintf(spec, sizeof(spec),
           "crop=%d:%d,split[a][b];[a]null[out_a];"
           "[b]hqdn3d=1.5:1.5:6:6[out_b]",
           cw, ch);

  ret = avfilter_graph_parse_ptr(*graph, spec, &inputs, &outputs, NULL);
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);
  if (ret < 0)
    return ret;

  return avfilter_graph_config(*graph, NULL);
}

/**
 * Extract a short sample of the source and a denoised twin to two FFV1/MKV
 * files.  Seeks to @p pos_ratio of the file's duration and runs for
 * @p duration_sec.
 */
static int extract_samples(const char *in_path, const char *out_src_path,
                           const char *out_denoised_path, double pos_ratio,
                           int duration_sec) {
  AVFormatContext *fmt = NULL;
  AVCodecContext *dec = NULL;
  AVFilterGraph *graph = NULL;
  AVFilterContext *src_ctx = NULL, *sink_a = NULL, *sink_b = NULL;
  OutCtx out_a = {0}, out_b = {0};
  AVPacket *pkt = NULL;
  AVFrame *frame = NULL, *filt_a = NULL, *filt_b = NULL;
  int outputs_open = 0;
  int ret;

  ret = avformat_open_input(&fmt, in_path, NULL, NULL);
  if (ret < 0)
    goto done;
  ret = avformat_find_stream_info(fmt, NULL);
  if (ret < 0)
    goto done;

  int vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (vidx < 0) {
    ret = vidx;
    goto done;
  }
  AVStream *stream = fmt->streams[vidx];

  const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!decoder) {
    ret = AVERROR_DECODER_NOT_FOUND;
    goto done;
  }
  dec = avcodec_alloc_context3(decoder);
  if (!dec) {
    ret = AVERROR(ENOMEM);
    goto done;
  }
  avcodec_parameters_to_context(dec, stream->codecpar);
  dec->pkt_timebase = stream->time_base;
  ret = avcodec_open2(dec, decoder, NULL);
  if (ret < 0)
    goto done;

  ret = build_graph(&graph, &src_ctx, &sink_a, &sink_b, dec);
  if (ret < 0)
    goto done;

  pkt = av_packet_alloc();
  frame = av_frame_alloc();
  filt_a = av_frame_alloc();
  filt_b = av_frame_alloc();
  if (!pkt || !frame || !filt_a || !filt_b) {
    ret = AVERROR(ENOMEM);
    goto done;
  }

  /* Seek to the requested position. */
  int64_t dur = fmt->duration > 0 ? fmt->duration : 600 * AV_TIME_BASE;
  if (pos_ratio < 0.0)
    pos_ratio = 0.0;
  if (pos_ratio > 0.95)
    pos_ratio = 0.95;
  int64_t target = (int64_t)((double)dur * pos_ratio);
  av_seek_frame(fmt, -1, target, AVSEEK_FLAG_BACKWARD);
  avcodec_flush_buffers(dec);

  /* Stop after ~duration_sec of decoded frames (in stream time). */
  int64_t start_pts = AV_NOPTS_VALUE;
  int64_t stop_after = av_rescale_q((int64_t)duration_sec * AV_TIME_BASE,
                                    AV_TIME_BASE_Q, stream->time_base);

  int done_reading = 0;
  while (!done_reading) {
    ret = av_read_frame(fmt, pkt);
    if (ret < 0)
      break;
    if (pkt->stream_index != vidx) {
      av_packet_unref(pkt);
      continue;
    }
    ret = avcodec_send_packet(dec, pkt);
    av_packet_unref(pkt);
    if (ret < 0)
      goto done;

    while (1) {
      ret = avcodec_receive_frame(dec, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0)
        goto done;

      if (start_pts == AV_NOPTS_VALUE)
        start_pts = frame->pts;

      if (av_buffersrc_add_frame(src_ctx, frame) < 0) {
        av_frame_unref(frame);
        continue;
      }
      av_frame_unref(frame);

      while (av_buffersink_get_frame(sink_a, filt_a) == 0) {
        if (!outputs_open) {
          /* Lazily open once we know the filtered pixel format. */
          AVRational tb = av_buffersink_get_time_base(sink_a);
          if ((ret = out_open(out_src_path, filt_a, tb, &out_a)) < 0 ||
              (ret = out_open(out_denoised_path, filt_a, tb, &out_b)) < 0) {
            av_frame_unref(filt_a);
            goto done;
          }
          outputs_open = 1;
        }
        ret = write_frame(&out_a, filt_a);
        av_frame_unref(filt_a);
        if (ret < 0)
          goto done;
      }

      while (av_buffersink_get_frame(sink_b, filt_b) == 0) {
        ret = write_frame(&out_b, filt_b);
        int64_t cur = filt_b->pts;
        av_frame_unref(filt_b);
        if (ret < 0)
          goto done;
        if (start_pts != AV_NOPTS_VALUE && cur != AV_NOPTS_VALUE &&
            cur - start_pts > stop_after) {
          done_reading = 1;
          break;
        }
      }
    }
  }

  /* Flush decoder. */
  avcodec_send_packet(dec, NULL);
  while (avcodec_receive_frame(dec, frame) == 0) {
    (void)av_buffersrc_add_frame(src_ctx, frame);
    av_frame_unref(frame);
  }
  (void)av_buffersrc_add_frame(src_ctx, NULL);

  /* Drain filter sinks. */
  if (outputs_open) {
    while (av_buffersink_get_frame(sink_a, filt_a) == 0) {
      write_frame(&out_a, filt_a);
      av_frame_unref(filt_a);
    }
    while (av_buffersink_get_frame(sink_b, filt_b) == 0) {
      write_frame(&out_b, filt_b);
      av_frame_unref(filt_b);
    }
  }

  ret = outputs_open ? 0 : AVERROR(EIO);

done:
  av_packet_free(&pkt);
  av_frame_free(&frame);
  av_frame_free(&filt_a);
  av_frame_free(&filt_b);
  if (outputs_open) {
    out_close(&out_a);
    out_close(&out_b);
  }
  avfilter_graph_free(&graph);
  avcodec_free_context(&dec);
  avformat_close_input(&fmt);
  return ret;
}

/* ------------------------------------------------------------------------- */
/*  grav1synth invocation                                                    */
/* ------------------------------------------------------------------------- */

static int spawn_grav1synth(const char *src, const char *denoised,
                            const char *table_out) {
  /* grav1synth diff <src> <denoised> -o <table_out> */
  char *argv[] = {(char *)VMAV_GRAV1SYNTH_BIN,
                  "diff",
                  (char *)src,
                  (char *)denoised,
                  "-o",
                  (char *)table_out,
                  NULL};
  pid_t pid;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  /* Silence grav1synth's chatter; we only care about exit status + table. */
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                   O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                   O_WRONLY, 0);
  int ret = posix_spawn(&pid, VMAV_GRAV1SYNTH_BIN, &actions, NULL, argv,
                        environ);
  posix_spawn_file_actions_destroy(&actions);
  if (ret != 0)
    return -ret;

  int status = 0;
  if (waitpid(pid, &status, 0) < 0)
    return -errno;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return -1;
  return 0;
}

/* ------------------------------------------------------------------------- */
/*  Grain-table parser                                                       */
/* ------------------------------------------------------------------------- */

/**
 * Parse a grav1synth grain table and return a 0..1 grain strength value.
 *
 * Format (tab-indented under each "E" line):
 *   filmgrn1
 *   E <start_ts> <end_ts> 1 <seed> 1
 *       p <12 params>
 *       sY <count> v1 s1 v2 s2 ...
 *       sCb <count> ...
 *       sCr <count> ...
 *       cY <coeffs>
 *       ...
 *
 * We average the Y scaling values (s1, s2, ...) across all scenes.
 * Each scaling is in [0, 255]; the final score is mean / 255.
 */
static double parse_grain_table(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return 0.0;

  double sum = 0.0;
  long count = 0;
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;

  while ((n = getline(&line, &cap, f)) >= 0) {
    char *p = line;
    while (*p == '\t' || *p == ' ')
      p++;
    if (p[0] != 's' || p[1] != 'Y' || (p[2] != ' ' && p[2] != '\t'))
      continue;
    p += 2;
    /* First integer after "sY" is the point count; skip it. */
    char *end;
    long pt_count = strtol(p, &end, 10);
    if (end == p || pt_count <= 0)
      continue;
    p = end;
    for (long i = 0; i < pt_count; i++) {
      /* Each point is "value scaling". We want the scaling. */
      strtol(p, &end, 10); /* value */
      if (end == p)
        break;
      p = end;
      long scaling = strtol(p, &end, 10);
      if (end == p)
        break;
      p = end;
      sum += (double)scaling;
      count++;
    }
  }

  free(line);
  fclose(f);
  if (count == 0)
    return 0.0;
  double mean = sum / (double)count;
  if (mean < 0)
    mean = 0;
  if (mean > 255.0)
    mean = 255.0;
  return mean / 255.0;
}

/* ------------------------------------------------------------------------- */
/*  Public entry point                                                       */
/* ------------------------------------------------------------------------- */

GrainScore get_grain_score(const char *path) {
  GrainScore result = {0};
  const int keep_tmp = getenv("VMAV_KEEP_GRAIN_TMP") != NULL;

  double max_score = 0.0;
  int windows_succeeded = 0;
  int first_error = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    char src_tmp[4096], denoised_tmp[4096], table_tmp[4096];
    snprintf(src_tmp, sizeof(src_tmp), "%s.grav1_src_%d.mkv", path, i);
    snprintf(denoised_tmp, sizeof(denoised_tmp), "%s.grav1_denoised_%d.mkv",
             path, i);
    snprintf(table_tmp, sizeof(table_tmp), "%s.grav1_table_%d.txt", path, i);

    int ret = extract_samples(path, src_tmp, denoised_tmp,
                              SAMPLE_POSITIONS[i], SAMPLE_DURATION_SEC);
    if (ret >= 0) {
      ret = spawn_grav1synth(src_tmp, denoised_tmp, table_tmp);
      if (ret >= 0) {
        struct stat st;
        if (stat(table_tmp, &st) == 0 && st.st_size > 0 &&
            st.st_size <= MAX_TABLE_BYTES) {
          double s = parse_grain_table(table_tmp);
          if (s > max_score)
            max_score = s;
          windows_succeeded++;
        } else {
          ret = AVERROR(EIO);
        }
      }
    }
    if (ret < 0 && first_error == 0)
      first_error = ret;

    if (!keep_tmp) {
      unlink(src_tmp);
      unlink(denoised_tmp);
      unlink(table_tmp);
    } else {
      fprintf(stderr, "[grain] window %d (pos %.2f): kept %s\n", i,
              SAMPLE_POSITIONS[i], table_tmp);
    }
  }

  if (windows_succeeded == 0) {
    result.error = first_error ? first_error : AVERROR(EIO);
    return result;
  }

  result.grain_score = max_score;
  result.avg_noise = max_score * 255.0;
  result.frames_analyzed = windows_succeeded * SAMPLE_DURATION_SEC * 30;
  return result;
}
