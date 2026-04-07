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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/**
 * @brief Append a shell-quoted copy of @p src to @p dst.
 *
 * Wraps the value in double quotes and backslash-escapes any
 * shell-significant characters.
 */
static void shell_quote_append(char *dst, size_t cap, size_t *pos,
                               const char *src) {
  if (*pos < cap)
    dst[(*pos)++] = '"';
  for (; src && *src; src++) {
    if (*pos + 2 >= cap)
      break;
    unsigned char c = (unsigned char)*src;
    if (c == '"' || c == '\\' || c == '$' || c == '`') {
      dst[(*pos)++] = '\\';
    }
    dst[(*pos)++] = (char)c;
  }
  if (*pos < cap)
    dst[(*pos)++] = '"';
  if (*pos < cap)
    dst[*pos] = '\0';
  else
    dst[cap - 1] = '\0';
}

/** @brief Append a printf-formatted string to the command buffer. */
static void cmd_appendf(char *buf, size_t cap, size_t *pos, const char *fmt,
                        ...) {
  if (*pos >= cap)
    return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
  va_end(ap);
  if (n > 0)
    *pos += (size_t)n;
}

FinalMuxResult final_mux(const FinalMuxConfig *config) {
  FinalMuxResult result = {.error = 0, .skipped = 0};

  /* Skip if output already exists. */
  struct stat st;
  if (stat(config->output_path, &st) == 0 && st.st_size > 0) {
    result.skipped = 1;
    return result;
  }

  /* Build the ffmpeg command. 128 KiB is comfortable headroom for dozens
     of tracks with metadata. */
  const size_t cap = 128 * 1024;
  char *cmd = malloc(cap);
  if (!cmd) {
    result.error = -1;
    return result;
  }
  size_t pos = 0;

  cmd_appendf(cmd, cap, &pos, "ffmpeg -y -loglevel error -nostdin");

  /* Inputs: video first, then audio, then subtitles. */
  cmd_appendf(cmd, cap, &pos, " -i ");
  shell_quote_append(cmd, cap, &pos, config->video_path);

  for (int i = 0; i < config->audio_count; i++) {
    cmd_appendf(cmd, cap, &pos, " -i ");
    shell_quote_append(cmd, cap, &pos, config->audio[i].path);
  }
  for (int i = 0; i < config->sub_count; i++) {
    cmd_appendf(cmd, cap, &pos, " -i ");
    shell_quote_append(cmd, cap, &pos, config->subs[i].path);
  }

  /* Optional chapters source: added as a trailing input, but only its
     chapters metadata is mapped into the output (no streams). */
  int chapters_idx = -1;
  if (config->chapters_source_path && config->chapters_source_path[0]) {
    chapters_idx = 1 + config->audio_count + config->sub_count;
    cmd_appendf(cmd, cap, &pos, " -i ");
    shell_quote_append(cmd, cap, &pos, config->chapters_source_path);
  }

  /* Stream maps. */
  cmd_appendf(cmd, cap, &pos, " -map 0:v:0");
  for (int i = 0; i < config->audio_count; i++)
    cmd_appendf(cmd, cap, &pos, " -map %d:a:0", 1 + i);
  for (int i = 0; i < config->sub_count; i++)
    cmd_appendf(cmd, cap, &pos, " -map %d:s:0?",
                1 + config->audio_count + i);

  /* Chapters: take from source file if provided, else drop entirely
     (the -1 sentinel disables chapters). */
  if (chapters_idx >= 0)
    cmd_appendf(cmd, cap, &pos, " -map_chapters %d", chapters_idx);
  else
    cmd_appendf(cmd, cap, &pos, " -map_chapters -1");

  /* Copy all streams, force SRT codec for text subs (safe no-op if already). */
  cmd_appendf(cmd, cap, &pos, " -c copy -c:s srt");

  /* Video stream: clear any default/forced flags we don't own. */
  cmd_appendf(cmd, cap, &pos, " -disposition:v:0 0");

  if (config->video_title && config->video_title[0]) {
    cmd_appendf(cmd, cap, &pos, " -metadata:s:v:0 title=");
    shell_quote_append(cmd, cap, &pos, config->video_title);
  }
  if (config->video_language && config->video_language[0]) {
    cmd_appendf(cmd, cap, &pos, " -metadata:s:v:0 language=%s",
                config->video_language);
  }

  /* Audio metadata + dispositions. */
  for (int i = 0; i < config->audio_count; i++) {
    const char *lang = (config->audio[i].language && config->audio[i].language[0])
                           ? config->audio[i].language
                           : "und";
    cmd_appendf(cmd, cap, &pos, " -metadata:s:a:%d language=%s", i, lang);
    if (config->audio[i].track_name && config->audio[i].track_name[0]) {
      cmd_appendf(cmd, cap, &pos, " -metadata:s:a:%d title=", i);
      shell_quote_append(cmd, cap, &pos, config->audio[i].track_name);
    }
    cmd_appendf(cmd, cap, &pos, " -disposition:a:%d %s", i,
                config->audio[i].is_default ? "default" : "0");
  }

  /* Subtitle metadata + dispositions. */
  for (int i = 0; i < config->sub_count; i++) {
    const char *lang = (config->subs[i].language && config->subs[i].language[0])
                           ? config->subs[i].language
                           : "und";
    cmd_appendf(cmd, cap, &pos, " -metadata:s:s:%d language=%s", i, lang);
    if (config->subs[i].track_name && config->subs[i].track_name[0]) {
      cmd_appendf(cmd, cap, &pos, " -metadata:s:s:%d title=", i);
      shell_quote_append(cmd, cap, &pos, config->subs[i].track_name);
    }

    /* Compose disposition string: default / forced / hearing_impaired. */
    char disp[96];
    disp[0] = '\0';
    size_t dpos = 0;
    int any = 0;
#define DISP_ADD(flag)                                                       \
  do {                                                                       \
    if (any)                                                                 \
      dpos += (size_t)snprintf(disp + dpos, sizeof(disp) - dpos, "+" flag);  \
    else                                                                     \
      dpos += (size_t)snprintf(disp + dpos, sizeof(disp) - dpos, flag);      \
    any = 1;                                                                 \
  } while (0)

    if (config->subs[i].is_default)
      DISP_ADD("default");
    if (config->subs[i].is_forced)
      DISP_ADD("forced");
    if (config->subs[i].is_sdh)
      DISP_ADD("hearing_impaired");
#undef DISP_ADD

    cmd_appendf(cmd, cap, &pos, " -disposition:s:%d %s", i,
                any ? disp : "0");
  }

  /* Container title. */
  if (config->title && config->title[0]) {
    cmd_appendf(cmd, cap, &pos, " -metadata title=");
    shell_quote_append(cmd, cap, &pos, config->title);
  }

  /* Matroska: don't auto-mark a subtitle as default when none is tagged. */
  cmd_appendf(cmd, cap, &pos, " -default_mode infer_no_subs");

  /* Output. */
  cmd_appendf(cmd, cap, &pos, " ");
  shell_quote_append(cmd, cap, &pos, config->output_path);

  if (pos >= cap - 1) {
    fprintf(stderr, "  Mux Error: command buffer overflow\n");
    free(cmd);
    result.error = -1;
    return result;
  }

  int rc = system(cmd);
  free(cmd);

  if (rc != 0) {
    fprintf(stderr, "  Mux Error: ffmpeg returned %d\n", rc);
    remove(config->output_path);
    result.error = rc == 0 ? -1 : rc;
    return result;
  }

  return result;
}
