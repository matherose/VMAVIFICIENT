#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Final MKV muxing: combine the AV1 video (from vmav_video_encode) with
 * pre-encoded opus audio tracks (vmav_audio_encode_track) and SRT
 * subtitles (vmav_subtitle_convert_pgs / passthrough text subs) into
 * the output .mkv.
 *
 * Implementation uses libavformat directly (no `ffmpeg` subprocess) so
 * vmavificient ships as a true single binary — no runtime PATH
 * dependency on the user having ffmpeg installed. Stream-copy is
 * preserved (every input's codecpar copied byte-for-byte into the
 * output), and per-stream metadata + dispositions are set the same
 * way the ffmpeg CLI would have. */

typedef struct {
    const char *path;
    const char *language; /* ISO 639-2/B 3-letter code, may be NULL */
    const char *track_name;
    bool is_default;
} vmav_mux_audio_t;

typedef struct {
    const char *path;
    const char *language;
    const char *track_name;
    bool is_default;
    bool is_forced;
    bool is_sdh;
} vmav_mux_sub_t;

typedef struct {
    const char *video_path;      /* .ivf */
    const char *output_path;     /* .mkv */
    const char *video_title;     /* optional, sent as -metadata:s:v:0 title= */
    const char *video_language;  /* ISO 639-2/B, optional */
    const char *container_title; /* optional, sent as -metadata title= */
    const char *chapters_source; /* path to file whose chapters we keep, or NULL */
    const vmav_mux_audio_t *audio;
    size_t audio_count;
    const vmav_mux_sub_t *subs;
    size_t sub_count;
} vmav_final_mux_spec_t;

typedef struct {
    char output_path[1024];
    bool skipped;
} vmav_final_mux_result_t;

/* Run the mux. Skips if output already exists non-empty. */
vmav_status_t vmav_final_mux(const vmav_final_mux_spec_t *spec, vmav_final_mux_result_t *out);

#ifdef __cplusplus
}
#endif
