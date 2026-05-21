#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Core video metadata extracted from a media file's primary video
 * stream. Populated by vmav_media_probe; consumers should ignore
 * every field on a non-OK return. */
typedef struct {
    int width; /* pixels */
    int height;
    double duration_s;       /* seconds, 0 if unknown */
    double framerate;        /* fps, 0 if unknown */
    int video_stream_index;  /* index of the primary video stream */
    char container_name[32]; /* e.g. "matroska,webm", "mov,mp4,..." */
    char codec_name[32];     /* e.g. "h264", "hevc", "av1" */
    int64_t bit_rate;        /* bits per second; 0 if unknown */
} vmav_media_info_t;

/* Open `path`, probe the container, locate the best video stream,
 * and populate `*out`. Returns:
 *   - VMAV_ERR_BAD_ARG    null path or null out
 *   - VMAV_ERR_IO         can't open path / can't read stream info
 *   - VMAV_ERR_NOT_FOUND  no video stream in the file
 *   - VMAV_ERR_FFMPEG     other libavformat failure (with av_make_error_string)
 * On VMAV_OK, every field of `*out` is meaningful. */
vmav_status_t vmav_media_probe(const char *path, vmav_media_info_t *out);

/* Render a media_info struct as a human-readable line via vmav_log
 * INFO. Useful from cmd_analyze. */
void vmav_media_info_log(const vmav_media_info_t *info, const char *path);

#ifdef __cplusplus
}
#endif
