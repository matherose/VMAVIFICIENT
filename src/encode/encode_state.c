/* vmav_encode_state — JSON-backed per-input encode progress, persisted
 * to <cache-dir>/state.json so a power-cut or Ctrl-C mid-encode can
 * resume from the last completed step instead of redoing the CRF
 * search from scratch. See include/vmavificient/vmav_encode_state.h
 * for the contract and the on-disk schema. */

#include "vmavificient/vmav_encode_state.h"
#include "vmavificient/vmav_os.h"

#include "../util/json_io.h"

#include <cJSON.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

/* =====================================================================
 *  Step status helpers
 * ===================================================================== */

const char *vmav_step_status_str(vmav_step_status_t s) {
    switch (s) {
    case VMAV_STEP_PENDING:
        return "pending";
    case VMAV_STEP_COMPLETE:
        return "complete";
    case VMAV_STEP_FAILED:
        return "failed";
    }
    return "pending";
}

vmav_step_status_t vmav_step_status_from_str(const char *s) {
    if (s == NULL) {
        return VMAV_STEP_PENDING;
    }
    if (strcmp(s, "complete") == 0) {
        return VMAV_STEP_COMPLETE;
    }
    if (strcmp(s, "failed") == 0) {
        return VMAV_STEP_FAILED;
    }
    return VMAV_STEP_PENDING;
}

/* =====================================================================
 *  Lifecycle
 * ===================================================================== */

void vmav_encode_state_init(vmav_encode_state_t *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->schema_version = VMAV_STATE_SCHEMA_VERSION;
}

void vmav_encode_state_free(vmav_encode_state_t *state) {
    if (state == NULL) {
        return;
    }
    free(state->audio);
    free(state->subs);
    memset(state, 0, sizeof(*state));
}

vmav_status_t vmav_encode_state_add_audio(vmav_encode_state_t *state,
                                          const vmav_state_audio_t *track) {
    if (state == NULL || track == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "add_audio: null arg");
    }
    if (state->audio_count >= state->audio_cap) {
        const size_t new_cap = state->audio_cap == 0 ? 4 : state->audio_cap * 2;
        vmav_state_audio_t *resized = realloc(state->audio, new_cap * sizeof(*resized));
        if (resized == NULL) {
            return VMAV_ERR(VMAV_ERR_NO_MEM, "realloc audio");
        }
        state->audio = resized;
        state->audio_cap = new_cap;
    }
    state->audio[state->audio_count++] = *track;
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_encode_state_add_sub(vmav_encode_state_t *state, const vmav_state_sub_t *track) {
    if (state == NULL || track == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "add_sub: null arg");
    }
    if (state->sub_count >= state->sub_cap) {
        const size_t new_cap = state->sub_cap == 0 ? 4 : state->sub_cap * 2;
        vmav_state_sub_t *resized = realloc(state->subs, new_cap * sizeof(*resized));
        if (resized == NULL) {
            return VMAV_ERR(VMAV_ERR_NO_MEM, "realloc subs");
        }
        state->subs = resized;
        state->sub_cap = new_cap;
    }
    state->subs[state->sub_count++] = *track;
    return VMAV_OK_STATUS;
}

/* =====================================================================
 *  Fingerprint + paths
 * ===================================================================== */

vmav_status_t vmav_encode_state_compute_fingerprint(const char *input_path,
                                                    int64_t *out_size,
                                                    int64_t *out_mtime) {
    if (input_path == NULL || out_size == NULL || out_mtime == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "compute_fingerprint: null arg");
    }
    struct stat st;
    if (stat(input_path, &st) != 0) {
        return VMAV_ERR(VMAV_ERR_IO, "stat '%s'", input_path);
    }
    *out_size = (int64_t)st.st_size;
    *out_mtime = (int64_t)st.st_mtime;
    return VMAV_OK_STATUS;
}

vmav_status_t
vmav_encode_state_default_cache_dir(const char *input_path, char *buf, size_t bufsize) {
    if (input_path == NULL || buf == NULL || bufsize == 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "default_cache_dir: null arg");
    }
    /* dirname(input_path) — POSIX-only path separator for now. The
     * Windows cross-build runs the binary under wine, where forward
     * slashes also work via the unix-mapped Z: drive. */
    const char *slash = strrchr(input_path, '/');
    const size_t dir_len = (slash != NULL) ? (size_t)(slash - input_path) : 0;
    const char *suffix = "/.vmavificient-cache";
    const size_t suffix_len = strlen(suffix);
    if (dir_len + suffix_len + 1 > bufsize) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "default_cache_dir: path too long");
    }
    if (dir_len > 0) {
        memcpy(buf, input_path, dir_len);
    } else {
        /* Input has no directory component — anchor at "." so the
         * cache lives in the cwd rather than at /.vmavificient-cache. */
        buf[0] = '.';
        memcpy(buf + 1, suffix, suffix_len);
        buf[1 + suffix_len] = '\0';
        return VMAV_OK_STATUS;
    }
    memcpy(buf + dir_len, suffix, suffix_len);
    buf[dir_len + suffix_len] = '\0';
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_encode_state_ensure_cache_dir(const char *cache_dir) {
    if (cache_dir == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "ensure_cache_dir: null arg");
    }
    return vmav_fs_mkdir_p(cache_dir);
}

vmav_status_t vmav_encode_state_path(const char *cache_dir, char *buf, size_t bufsize) {
    if (cache_dir == NULL || buf == NULL || bufsize == 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "state_path: null arg");
    }
    const int n = snprintf(buf, bufsize, "%s/state.json", cache_dir);
    if (n < 0 || (size_t)n >= bufsize) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "state_path: overflow");
    }
    return VMAV_OK_STATUS;
}

/* =====================================================================
 *  JSON serialization helpers
 * ===================================================================== */

static cJSON *step_to_json(vmav_step_status_t s) {
    return cJSON_CreateString(vmav_step_status_str(s));
}

static vmav_step_status_t json_to_step(const cJSON *node, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(node, key);
    if (v == NULL || !cJSON_IsString(v)) {
        return VMAV_STEP_PENDING;
    }
    return vmav_step_status_from_str(v->valuestring);
}

static cJSON *crop_to_json(const vmav_state_crop_t *c) {
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddItemToObject(o, "status", step_to_json(c->status));
    cJSON_AddNumberToObject(o, "x", c->x);
    cJSON_AddNumberToObject(o, "y", c->y);
    cJSON_AddNumberToObject(o, "width", c->width);
    cJSON_AddNumberToObject(o, "height", c->height);
    cJSON_AddBoolToObject(o, "is_meaningful", c->is_meaningful);
    return o;
}

static void crop_from_json(const cJSON *root, vmav_state_crop_t *out) {
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, "crop");
    if (o == NULL) {
        return;
    }
    out->status = json_to_step(o, "status");
    out->x = vmav_json_get_int(o, "x", 0);
    out->y = vmav_json_get_int(o, "y", 0);
    out->width = vmav_json_get_int(o, "width", 0);
    out->height = vmav_json_get_int(o, "height", 0);
    out->is_meaningful = vmav_json_get_bool(o, "is_meaningful", false);
}

static cJSON *grain_to_json(const vmav_state_grain_t *g) {
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddItemToObject(o, "status", step_to_json(g->status));
    cJSON_AddNumberToObject(o, "score", g->score);
    cJSON_AddNumberToObject(o, "variance", g->variance);
    return o;
}

static void grain_from_json(const cJSON *root, vmav_state_grain_t *out) {
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, "grain");
    if (o == NULL) {
        return;
    }
    out->status = json_to_step(o, "status");
    out->score = vmav_json_get_number(o, "score", 0.0);
    out->variance = vmav_json_get_number(o, "variance", 0.0);
}

static cJSON *crf_to_json(const vmav_state_crf_t *c) {
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddItemToObject(o, "status", step_to_json(c->status));
    cJSON_AddNumberToObject(o, "crf", c->crf);
    cJSON_AddNumberToObject(o, "vmaf", c->vmaf);
    cJSON_AddBoolToObject(o, "escalated", c->escalated);
    return o;
}

static void crf_from_json(const cJSON *root, vmav_state_crf_t *out) {
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, "crf");
    if (o == NULL) {
        return;
    }
    out->status = json_to_step(o, "status");
    out->crf = vmav_json_get_int(o, "crf", 0);
    out->vmaf = vmav_json_get_number(o, "vmaf", 0.0);
    out->escalated = vmav_json_get_bool(o, "escalated", false);
}

static cJSON *audio_to_json(const vmav_state_audio_t *a) {
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddItemToObject(o, "status", step_to_json(a->status));
    cJSON_AddNumberToObject(o, "stream_index", a->stream_index);
    cJSON_AddStringToObject(o, "language", a->language);
    cJSON_AddStringToObject(o, "title", a->title);
    cJSON_AddBoolToObject(o, "is_default", a->is_default);
    cJSON_AddStringToObject(o, "output_path", a->output_path);
    return o;
}

static void audio_from_json(const cJSON *node, vmav_state_audio_t *out) {
    memset(out, 0, sizeof(*out));
    out->status = json_to_step(node, "status");
    out->stream_index = vmav_json_get_int(node, "stream_index", -1);
    snprintf(
        out->language, sizeof(out->language), "%s", vmav_json_get_string(node, "language", ""));
    snprintf(out->title, sizeof(out->title), "%s", vmav_json_get_string(node, "title", ""));
    out->is_default = vmav_json_get_bool(node, "is_default", false);
    snprintf(out->output_path,
             sizeof(out->output_path),
             "%s",
             vmav_json_get_string(node, "output_path", ""));
}

static cJSON *sub_to_json(const vmav_state_sub_t *s) {
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddItemToObject(o, "status", step_to_json(s->status));
    cJSON_AddNumberToObject(o, "stream_index", s->stream_index);
    cJSON_AddStringToObject(o, "language", s->language);
    cJSON_AddBoolToObject(o, "is_forced", s->is_forced);
    cJSON_AddBoolToObject(o, "is_sdh", s->is_sdh);
    cJSON_AddStringToObject(o, "output_path", s->output_path);
    cJSON_AddNumberToObject(o, "subtitle_count", s->subtitle_count);
    return o;
}

static void sub_from_json(const cJSON *node, vmav_state_sub_t *out) {
    memset(out, 0, sizeof(*out));
    out->status = json_to_step(node, "status");
    out->stream_index = vmav_json_get_int(node, "stream_index", -1);
    snprintf(
        out->language, sizeof(out->language), "%s", vmav_json_get_string(node, "language", ""));
    out->is_forced = vmav_json_get_bool(node, "is_forced", false);
    out->is_sdh = vmav_json_get_bool(node, "is_sdh", false);
    snprintf(out->output_path,
             sizeof(out->output_path),
             "%s",
             vmav_json_get_string(node, "output_path", ""));
    out->subtitle_count = vmav_json_get_int(node, "subtitle_count", 0);
}

static cJSON *video_to_json(const vmav_state_video_t *v) {
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddItemToObject(o, "status", step_to_json(v->status));
    cJSON_AddStringToObject(o, "output_path", v->output_path);
    cJSON_AddNumberToObject(o, "frame_count", (double)v->frame_count);
    return o;
}

static void video_from_json(const cJSON *root, vmav_state_video_t *out) {
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, "video");
    if (o == NULL) {
        return;
    }
    out->status = json_to_step(o, "status");
    snprintf(out->output_path,
             sizeof(out->output_path),
             "%s",
             vmav_json_get_string(o, "output_path", ""));
    out->frame_count = (int64_t)vmav_json_get_number(o, "frame_count", 0);
}

static cJSON *mux_to_json(const vmav_state_mux_t *m) {
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddItemToObject(o, "status", step_to_json(m->status));
    cJSON_AddStringToObject(o, "output_path", m->output_path);
    return o;
}

static void mux_from_json(const cJSON *root, vmav_state_mux_t *out) {
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, "mux");
    if (o == NULL) {
        return;
    }
    out->status = json_to_step(o, "status");
    snprintf(out->output_path,
             sizeof(out->output_path),
             "%s",
             vmav_json_get_string(o, "output_path", ""));
}

/* =====================================================================
 *  load / save
 * ===================================================================== */

vmav_status_t vmav_encode_state_load(const char *cache_dir,
                                     const char *input_path,
                                     vmav_encode_state_t *out,
                                     bool *out_fingerprint_match) {
    if (cache_dir == NULL || input_path == NULL || out == NULL || out_fingerprint_match == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "encode_state_load: null arg");
    }
    vmav_encode_state_init(out);
    *out_fingerprint_match = false;

    /* Always compute current fingerprint so a successful load can
     * populate the input fields whether or not the file existed. */
    int64_t cur_size = 0;
    int64_t cur_mtime = 0;
    const vmav_status_t fp_st =
        vmav_encode_state_compute_fingerprint(input_path, &cur_size, &cur_mtime);
    if (!vmav_status_ok(fp_st)) {
        return fp_st;
    }
    snprintf(out->input_path, sizeof(out->input_path), "%s", input_path);
    out->input_size = cur_size;
    out->input_mtime = cur_mtime;

    char state_path[1280];
    const vmav_status_t pst = vmav_encode_state_path(cache_dir, state_path, sizeof(state_path));
    if (!vmav_status_ok(pst)) {
        return pst;
    }

    /* Missing file is a soft state: empty state + fingerprint_match=false.
     * Any real parse error bubbles up. */
    if (!vmav_fs_exists(state_path)) {
        return VMAV_OK_STATUS;
    }

    cJSON *root = NULL;
    const vmav_status_t rst = vmav_json_read_file(state_path, &root);
    if (!vmav_status_ok(rst)) {
        return rst;
    }

    const int sv = vmav_json_get_int(root, "schema_version", 0);
    if (sv != VMAV_STATE_SCHEMA_VERSION) {
        /* Unknown schema — treat as missing rather than crashing. */
        vmav_json_free(root);
        return VMAV_OK_STATUS;
    }

    const cJSON *input_node = cJSON_GetObjectItemCaseSensitive(root, "input");
    if (input_node != NULL && cJSON_IsObject(input_node)) {
        const int64_t loaded_size = (int64_t)vmav_json_get_number(input_node, "size", 0);
        const int64_t loaded_mtime = (int64_t)vmav_json_get_number(input_node, "mtime", 0);
        if (loaded_size == cur_size && loaded_mtime == cur_mtime) {
            *out_fingerprint_match = true;
        }
    }

    if (!*out_fingerprint_match) {
        /* Input changed under us. Throw away the loaded state — the
         * caller will start a fresh encode. */
        vmav_json_free(root);
        return VMAV_OK_STATUS;
    }

    crop_from_json(root, &out->crop);
    grain_from_json(root, &out->grain);
    crf_from_json(root, &out->crf);
    video_from_json(root, &out->video);
    mux_from_json(root, &out->mux);

    const cJSON *audio_arr = cJSON_GetObjectItemCaseSensitive(root, "audio");
    if (audio_arr != NULL && cJSON_IsArray(audio_arr)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, audio_arr) {
            vmav_state_audio_t t;
            audio_from_json(item, &t);
            const vmav_status_t ast = vmav_encode_state_add_audio(out, &t);
            if (!vmav_status_ok(ast)) {
                vmav_json_free(root);
                return ast;
            }
        }
    }

    const cJSON *sub_arr = cJSON_GetObjectItemCaseSensitive(root, "subs");
    if (sub_arr != NULL && cJSON_IsArray(sub_arr)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, sub_arr) {
            vmav_state_sub_t t;
            sub_from_json(item, &t);
            const vmav_status_t sst = vmav_encode_state_add_sub(out, &t);
            if (!vmav_status_ok(sst)) {
                vmav_json_free(root);
                return sst;
            }
        }
    }

    vmav_json_free(root);
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_encode_state_save(const char *cache_dir, const vmav_encode_state_t *state) {
    if (cache_dir == NULL || state == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "encode_state_save: null arg");
    }
    vmav_status_t st = vmav_encode_state_ensure_cache_dir(cache_dir);
    if (!vmav_status_ok(st)) {
        return st;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "cJSON_CreateObject");
    }
    cJSON_AddNumberToObject(root, "schema_version", state->schema_version);

    cJSON *input = cJSON_CreateObject();
    if (input == NULL) {
        cJSON_Delete(root);
        return VMAV_ERR(VMAV_ERR_NO_MEM, "cJSON_CreateObject input");
    }
    cJSON_AddStringToObject(input, "path", state->input_path);
    cJSON_AddNumberToObject(input, "size", (double)state->input_size);
    cJSON_AddNumberToObject(input, "mtime", (double)state->input_mtime);
    cJSON_AddItemToObject(root, "input", input);

    cJSON_AddItemToObject(root, "crop", crop_to_json(&state->crop));
    cJSON_AddItemToObject(root, "grain", grain_to_json(&state->grain));
    cJSON_AddItemToObject(root, "crf", crf_to_json(&state->crf));

    cJSON *audio_arr = cJSON_CreateArray();
    for (size_t i = 0; i < state->audio_count; i++) {
        cJSON_AddItemToArray(audio_arr, audio_to_json(&state->audio[i]));
    }
    cJSON_AddItemToObject(root, "audio", audio_arr);

    cJSON *sub_arr = cJSON_CreateArray();
    for (size_t i = 0; i < state->sub_count; i++) {
        cJSON_AddItemToArray(sub_arr, sub_to_json(&state->subs[i]));
    }
    cJSON_AddItemToObject(root, "subs", sub_arr);

    cJSON_AddItemToObject(root, "video", video_to_json(&state->video));
    cJSON_AddItemToObject(root, "mux", mux_to_json(&state->mux));

    char state_path[1280];
    const vmav_status_t pst = vmav_encode_state_path(cache_dir, state_path, sizeof(state_path));
    if (!vmav_status_ok(pst)) {
        cJSON_Delete(root);
        return pst;
    }
    st = vmav_json_write_atomic(state_path, root);
    cJSON_Delete(root);
    return st;
}
