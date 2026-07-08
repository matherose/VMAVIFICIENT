/**
 * @file final_mux.c
 * @brief Final MKV muxing: video + audio + subtitles with track names.
 *
 * Shells out to the ffmpeg CLI with `-c copy` for a rock-solid remux that
 * preserves OPUS pre-skip, subtitle codec params, and HDR/DV video side data.
 * The previous libavformat-based path produced MKVs where audio streams were
 * present but silent (tracks "deactivated"); this implementation avoids that
 * class of issue entirely.
 */

#include "final_mux.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "proc.h"

FinalMuxResult final_mux(const FinalMuxConfig *config) {
  FinalMuxResult result = {.error = 0, .skipped = 0};

  /* Skip if output already exists. */
  struct stat st;
  if (stat(config->output_path, &st) == 0 && st.st_size > 0) {
    result.skipped = 1;
    return result;
  }

  VmavCommand c;
  vmav_cmd_init(&c);

  vmav_cmd_arg(&c, "ffmpeg");
  vmav_cmd_arg(&c, "-y");
  vmav_cmd_arg(&c, "-loglevel");
  vmav_cmd_arg(&c, "error");
  vmav_cmd_arg(&c, "-nostdin");

  /* Inputs: video first, then audio, then subtitles. */
  vmav_cmd_arg(&c, "-i");
  vmav_cmd_arg(&c, config->video_path);

  for (int i = 0; i < config->audio_count; i++) {
    vmav_cmd_arg(&c, "-i");
    vmav_cmd_arg(&c, config->audio[i].path);
  }
  for (int i = 0; i < config->sub_count; i++) {
    vmav_cmd_arg(&c, "-i");
    vmav_cmd_arg(&c, config->subs[i].path);
  }

  /* Optional chapters source: added as a trailing input, but only its
     chapters metadata is mapped into the output (no streams). */
  int chapters_idx = -1;
  if (config->chapters_source_path && config->chapters_source_path[0]) {
    chapters_idx = 1 + config->audio_count + config->sub_count;
    vmav_cmd_arg(&c, "-i");
    vmav_cmd_arg(&c, config->chapters_source_path);
  }

  /* Stream maps. */
  vmav_cmd_arg(&c, "-map");
  vmav_cmd_arg(&c, "0:v:0");
  for (int i = 0; i < config->audio_count; i++) {
    vmav_cmd_arg(&c, "-map");
    vmav_cmd_argf(&c, "%d:a:0", 1 + i);
  }
  for (int i = 0; i < config->sub_count; i++) {
    vmav_cmd_arg(&c, "-map");
    vmav_cmd_argf(&c, "%d:s:0?", 1 + config->audio_count + i);
  }

  /* Chapters: take from source file if provided, else drop entirely
     (the -1 sentinel disables chapters). */
  vmav_cmd_arg(&c, "-map_chapters");
  if (chapters_idx >= 0)
    vmav_cmd_argf(&c, "%d", chapters_idx);
  else
    vmav_cmd_arg(&c, "-1");

  /* Copy all streams, force SRT codec for text subs (safe no-op if already). */
  vmav_cmd_arg(&c, "-c");
  vmav_cmd_arg(&c, "copy");
  vmav_cmd_arg(&c, "-c:s");
  vmav_cmd_arg(&c, "srt");

  /* Video stream: clear any default/forced flags we don't own. */
  vmav_cmd_arg(&c, "-disposition:v:0");
  vmav_cmd_arg(&c, "0");

  if (config->video_title && config->video_title[0]) {
    vmav_cmd_arg(&c, "-metadata:s:v:0");
    vmav_cmd_argf(&c, "title=%s", config->video_title);
  }
  if (config->video_language && config->video_language[0]) {
    vmav_cmd_arg(&c, "-metadata:s:v:0");
    vmav_cmd_argf(&c, "language=%s", config->video_language);
  }

  /* Audio metadata + dispositions. */
  for (int i = 0; i < config->audio_count; i++) {
    const char *lang = (config->audio[i].language && config->audio[i].language[0])
                           ? config->audio[i].language
                           : "und";
    vmav_cmd_argf(&c, "-metadata:s:a:%d", i);
    vmav_cmd_argf(&c, "language=%s", lang);
    if (config->audio[i].track_name && config->audio[i].track_name[0]) {
      vmav_cmd_argf(&c, "-metadata:s:a:%d", i);
      vmav_cmd_argf(&c, "title=%s", config->audio[i].track_name);
    }
    vmav_cmd_argf(&c, "-disposition:a:%d", i);
    vmav_cmd_arg(&c, config->audio[i].is_default ? "default" : "0");
  }

  /* Subtitle metadata + dispositions. */
  for (int i = 0; i < config->sub_count; i++) {
    const char *lang = (config->subs[i].language && config->subs[i].language[0])
                           ? config->subs[i].language
                           : "und";
    vmav_cmd_argf(&c, "-metadata:s:s:%d", i);
    vmav_cmd_argf(&c, "language=%s", lang);
    if (config->subs[i].track_name && config->subs[i].track_name[0]) {
      vmav_cmd_argf(&c, "-metadata:s:s:%d", i);
      vmav_cmd_argf(&c, "title=%s", config->subs[i].track_name);
    }

    /* Compose disposition string: default / forced / hearing_impaired. */
    char disp[96];
    disp[0] = '\0';
    size_t dpos = 0;
    int any = 0;
#define DISP_ADD(flag)                                                      \
  do {                                                                      \
    if (any)                                                                \
      dpos += (size_t)snprintf(disp + dpos, sizeof(disp) - dpos, "+" flag); \
    else                                                                    \
      dpos += (size_t)snprintf(disp + dpos, sizeof(disp) - dpos, flag);     \
    any = 1;                                                                \
  } while (0)

    if (config->subs[i].is_default)
      DISP_ADD("default");
    if (config->subs[i].is_forced)
      DISP_ADD("forced");
    if (config->subs[i].is_sdh)
      DISP_ADD("hearing_impaired");
#undef DISP_ADD

    vmav_cmd_argf(&c, "-disposition:s:%d", i);
    vmav_cmd_arg(&c, any ? disp : "0");
  }

  /* Container title. */
  if (config->title && config->title[0]) {
    vmav_cmd_arg(&c, "-metadata");
    vmav_cmd_argf(&c, "title=%s", config->title);
  }

  /* Matroska: don't auto-mark a subtitle as default when none is tagged. */
  vmav_cmd_arg(&c, "-default_mode");
  vmav_cmd_arg(&c, "infer_no_subs");

  /* Output. */
  vmav_cmd_arg(&c, config->output_path);

  if (c.overflow) {
    fprintf(stderr, "  Mux Error: command buffer overflow\n");
    result.error = -1;
    return result;
  }

  int exit_code = vmav_run(c.argv);
  if (exit_code != 0) {
    fprintf(stderr, "  Mux Error: ffmpeg returned %d\n", exit_code);
    remove(config->output_path);
    result.error = exit_code;
    return result;
  }

  return result;
}
