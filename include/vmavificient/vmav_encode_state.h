#pragma once

#include "vmavificient/vmav_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On-disk encode state for resumable encodes.
 *
 * The encode pipeline (crop → grain → CRF → audio → subs → video → mux)
 * persists its progress as JSON at <cache-dir>/state.json. cmd_encode
 * loads this on startup, verifies the input fingerprint, and skips any
 * step that already has status=COMPLETE — so a power-cut, a Ctrl-C, or
 * even a crash mid-mux is resumable without redoing the CRF search
 * (which is the most expensive computation).
 *
 * The schema is versioned (`schema_version`). On mismatch we treat the
 * state as missing and start fresh, but newer schema-version states
 * are never silently downgraded. The input fingerprint is captured at
 * first load and re-checked on resume; a changed size or mtime
 * invalidates the cache and forces a full rerun.
 *
 * Concurrency model: a single cmd_encode owns the cache_dir for the
 * duration of its run. No locking — multiple processes targeting the
 * same input would corrupt state. */

#define VMAV_STATE_SCHEMA_VERSION 1

typedef enum {
    VMAV_STEP_PENDING = 0,
    VMAV_STEP_COMPLETE,
    VMAV_STEP_FAILED,
} vmav_step_status_t;

const char *vmav_step_status_str(vmav_step_status_t s);
vmav_step_status_t vmav_step_status_from_str(const char *s);

typedef struct {
    vmav_step_status_t status;
    /* tmdb_id == 0 means "blind" (--blind or no --tmdb): use stem-based
     * output name. Non-zero means the TMDB lookup completed and the
     * title/year/original_lang fields are populated. */
    int tmdb_id;
    char title[512];       /* TMDB original_title, UTF-8 */
    int year;              /* TMDB release_year */
    char original_lang[8]; /* TMDB original_language (ISO 639-1) */
} vmav_state_tmdb_t;

typedef struct {
    vmav_step_status_t status;
    /* Crop rectangle in source pixels, mirrors vmav_crop_rect_t. */
    int x;
    int y;
    int width;
    int height;
    bool is_meaningful; /* false means "no black bars worth cropping" */
} vmav_state_crop_t;

typedef struct {
    vmav_step_status_t status;
    /* Grain score in [0..1]; mapped to SVT-AV1's film_grain knob by
     * vmav_svtav1_film_grain_from_score. Mirrors vmav_grain_score_t. */
    double score;
    double variance; /* per-window spread; 0 in the m5 baseline impl */
} vmav_state_grain_t;

typedef struct {
    vmav_step_status_t status;
    /* Exactly one of crf / bitrate_kbps is non-zero on COMPLETE:
     *   crf > 0          → CRF mode (search result, --crf override, or
     *                      preset default).
     *   bitrate_kbps > 0 → VBR mode (--bitrate override).
     * vmaf is meaningful for CRF mode only (0 for VBR / --crf overrides
     * that didn't run a search).
     * escalated is meaningful for CRF mode when the search ran. */
    int crf;
    int bitrate_kbps;
    double vmaf;
    bool escalated; /* true if CRF search needed pass 2 (3 samples) */
} vmav_state_crf_t;

typedef struct {
    vmav_step_status_t status;
    int stream_index; /* input stream index, for dedup */
    char language[8]; /* ISO 639-2/B, e.g. "eng" / "fre" */
    char title[64];   /* from track metadata, may be empty */
    bool is_default;
    char output_path[1024]; /* .opus sidecar */
} vmav_state_audio_t;

typedef struct {
    vmav_step_status_t status;
    int stream_index;
    char language[8];
    bool is_forced;
    bool is_sdh;
    char output_path[1024]; /* .srt sidecar */
    int subtitle_count;     /* OCR'd events; 0 means skipped at mux */
} vmav_state_sub_t;

typedef struct {
    vmav_step_status_t status;
    char output_path[1024]; /* .ivf intermediate */
    int64_t frame_count;
} vmav_state_video_t;

typedef struct {
    vmav_step_status_t status;
    char output_path[1024]; /* final .mkv */
} vmav_state_mux_t;

typedef struct {
    int schema_version;
    char input_path[1024];
    int64_t input_size;  /* bytes, from stat */
    int64_t input_mtime; /* unix epoch seconds, from stat */

    vmav_state_tmdb_t tmdb;
    vmav_state_crop_t crop;
    vmav_state_grain_t grain;
    vmav_state_crf_t crf;

    vmav_state_audio_t *audio;
    size_t audio_count;
    size_t audio_cap;

    vmav_state_sub_t *subs;
    size_t sub_count;
    size_t sub_cap;

    vmav_state_video_t video;
    vmav_state_mux_t mux;

    /* --companion-hd: HD downscale pass. Both stay PENDING when the
     * flag isn't set; serialized either way so older state.json files
     * resume without churn (missing fields → PENDING via cJSON null
     * lookup). HD reuses the UHD CRF (state.crf) — no separate
     * crf_hd in the schema. */
    vmav_state_video_t video_hd;
    vmav_state_mux_t mux_hd;
} vmav_encode_state_t;

/* Lifecycle. */
void vmav_encode_state_init(vmav_encode_state_t *state);
void vmav_encode_state_free(vmav_encode_state_t *state);

/* Append helpers. Returns OK or VMAV_ERR_NO_MEM on realloc failure. */
vmav_status_t vmav_encode_state_add_audio(vmav_encode_state_t *state,
                                          const vmav_state_audio_t *track);
vmav_status_t vmav_encode_state_add_sub(vmav_encode_state_t *state, const vmav_state_sub_t *track);

/* Fingerprint = (size, mtime). Future schemas can extend with a partial
 * SHA256 of the input's first MB; size+mtime catches >99% of "user
 * changed the input" cases without the I/O cost. */
vmav_status_t vmav_encode_state_compute_fingerprint(const char *input_path,
                                                    int64_t *out_size,
                                                    int64_t *out_mtime);

/* Default cache dir: `<dirname(input_path)>/.vmavificient-cache`.
 * Caller-supplied buffer; returns BAD_ARG on overflow. */
vmav_status_t
vmav_encode_state_default_cache_dir(const char *input_path, char *buf, size_t bufsize);

/* Create the cache dir if it doesn't exist. */
vmav_status_t vmav_encode_state_ensure_cache_dir(const char *cache_dir);

/* Compose `<cache_dir>/state.json`. */
vmav_status_t vmav_encode_state_path(const char *cache_dir, char *buf, size_t bufsize);

/* Load state from <cache_dir>/state.json.
 *
 * Sets *out_fingerprint_match to:
 *   - true  if the file existed AND the loaded fingerprint matches the
 *           current size+mtime of `input_path` → safe to resume.
 *   - false if the file was missing, malformed, or the fingerprint
 *           differs → caller should treat `out` as a fresh state.
 *
 * On false, `out` is initialized empty (vmav_encode_state_init) and
 * the input_path / fingerprint fields are populated from the current
 * input — caller can save() right away to materialize the state file.
 *
 * Returns VMAV_OK_STATUS in both cases; a non-OK return only happens
 * on I/O errors stat'ing the input or on hard JSON parse errors. */
vmav_status_t vmav_encode_state_load(const char *cache_dir,
                                     const char *input_path,
                                     vmav_encode_state_t *out,
                                     bool *out_fingerprint_match);

/* Serialize state to <cache_dir>/state.json atomically (write to tmp,
 * fsync, rename). Idempotent — safe to call after every step. */
vmav_status_t vmav_encode_state_save(const char *cache_dir, const vmav_encode_state_t *state);

#ifdef __cplusplus
}
#endif
