#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-track metadata for one audio or subtitle stream. The selection
 * + ranking logic (best per language, French variant detection) is
 * NOT included here — that lands in Phase 4 when the mux pipeline
 * needs it. For now we only enumerate; cmd_analyze and media_naming
 * consume the bare list. */
typedef struct {
    int stream_index;    /* index in the container */
    char name[256];      /* track title from metadata, may be empty */
    char language[8];    /* ISO 639-2/B (3 letters) if known, else "" */
    char codec_name[32]; /* e.g. "aac", "ac3", "srt", "hdmv_pgs_subtitle" */
    int codec_id;        /* AVCodecID as int (Phase 4 uses for ranking) */
    int channels;        /* audio: number of channels; subtitle: 0 */
    int profile;         /* codec profile (DTS-HD MA vs plain DTS) */
    int64_t bit_rate;    /* bits/s, 0 if unknown */
    bool is_forced;      /* disposition or title hint */
    bool is_sdh;         /* disposition or title hint */
} vmav_track_t;

/* Owned arrays. Free via vmav_media_tracks_free. */
typedef struct {
    vmav_track_t *audio;
    size_t audio_count;
    vmav_track_t *subtitle;
    size_t subtitle_count;
} vmav_media_tracks_t;

/* Open `path`, enumerate audio + subtitle streams, fill `*out`.
 * Returns the same error codes as vmav_media_probe. Caller MUST call
 * vmav_media_tracks_free on `*out` even on partial failure (the struct
 * is zero-initialized first so freeing a failed result is safe). */
vmav_status_t vmav_media_tracks_probe(const char *path, vmav_media_tracks_t *out);

/* Release heap arrays. Idempotent; safe on NULL or zero-init. */
void vmav_media_tracks_free(vmav_media_tracks_t *t);

#ifdef __cplusplus
}
#endif
