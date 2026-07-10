/**
 * @file pipeline_util.c
 * @brief Shared utility functions for the vmavificient pipeline.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pipeline.h"
#include "proc.h"
#include "ui.h"

/**
 * @brief Parse a string as a base-10 int.
 *
 * Returns the parsed value, or 0 if the string is NULL, empty, contains
 * non-numeric content (besides leading/trailing whitespace and trailing
 * newlines from fgets), or overflows int. Replaces atoi() — the
 * cert-err34-c rule for the codebase.
 */
int vmav_parse_int_or_zero(const char *s) {
  if (!s)
    return 0;
  char *endptr = NULL;
  errno = 0;
  long v = strtol(s, &endptr, 10);
  if (endptr == s)
    return 0; /* no digits parsed */
  /* Skip trailing whitespace; reject anything else. */
  while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' || *endptr == '\r')
    endptr++;
  if (*endptr != '\0')
    return 0;
  if (errno == ERANGE || v > INT_MAX || v < INT_MIN)
    return 0;
  return (int)v;
}

/* ---- Score cache helpers ---- */

bool vmav_file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

/* ---- Track display helpers ---- */

const char *vmav_codec_short(const char *codec) {
  if (strcmp(codec, "hdmv_pgs_subtitle") == 0)
    return "pgs";
  if (strcmp(codec, "subrip") == 0)
    return "srt";
  if (strcmp(codec, "dvd_subtitle") == 0)
    return "vob";
  if (strcmp(codec, "ass") == 0 || strcmp(codec, "ssa") == 0)
    return "ass";
  return codec;
}

/**
 * @brief Build a path inside the cache directory.
 * Appends the given relative path to ctx->cache_dir with proper separator.
 */
void vmav_build_cache_path(const PipelineCtx *ctx, char *buf, size_t bufsize,
                           const char *relative_path) {
  if (strlen(ctx->cache_dir) > 0 && ctx->cache_dir[strlen(ctx->cache_dir) - 1] == '/')
    snprintf(buf, bufsize, "%s%s", ctx->cache_dir, relative_path);
  else
    snprintf(buf, bufsize, "%s/%s", ctx->cache_dir, relative_path);
}

/**
 * @brief Remove the entire cache directory and recreate it.
 * Used after successful encode to cleanup all intermediate files.
 *
 * Atomic behavior: first renames the cache to a temporary name,
 * then creates a new empty cache dir. If rename fails, creates
 * cache dir in place. Old cache is deleted only after successful recreate.
 */
void vmav_cleanup_cache_dir(const PipelineCtx *ctx) {
  char old_cache_path[4096];
  pid_t pid = getpid();
  snprintf(old_cache_path, sizeof(old_cache_path), "%s.tmp.%d", ctx->cache_dir, pid);

  /* Try to atomically rename the cache directory */
  if (rename(ctx->cache_dir, old_cache_path) == 0) {
    /* Rename succeeded: create fresh cache dir */
    if (mkdir(ctx->cache_dir, 0755) != 0 && errno != EEXIST) {
      (void)fprintf(stderr, "Warning: failed to recreate cache directory '%s' (errno %d)\n",
                    ctx->cache_dir, errno);
      return;
    }
    /* Delete old cache in background (non-blocking) */
    vmav_rmtree_async(old_cache_path);
  } else {
    /* Rename failed (e.g., cache doesn't exist or is in use)
     * Try to recreate in place */
    if (vmav_rmtree(ctx->cache_dir) != 0) {
      (void)fprintf(stderr, "Warning: failed to cleanup cache directory '%s'\n", ctx->cache_dir);
    }
    /* Recreate empty cache directory */
    if (mkdir(ctx->cache_dir, 0755) != 0 && errno != EEXIST) {
      (void)fprintf(stderr, "Warning: failed to recreate cache directory '%s' (errno %d)\n",
                    ctx->cache_dir, errno);
    }
  }
}

bool vmav_load_cached_scores(const char *cache_path, double *grain_score, double *grain_variance,
                             int *crf) {
  FILE *f = fopen(cache_path, "r");
  if (!f)
    return false;
  char line[4096];
  bool found_grain = false, found_var = false, found_crf = false;
  while (fgets(line, sizeof(line), f)) {
    char value[64];
    if (sscanf(line, " \"grain_score\": %63[^,\"]", value) == 1)
      *grain_score = strtod(value, NULL), found_grain = true;
    else if (sscanf(line, " \"grain_variance\": %63[^,\"]", value) == 1)
      *grain_variance = strtod(value, NULL), found_var = true;
    else if (sscanf(line, " \"crf\": %63[^,\"]", value) == 1)
      *crf = (int)strtol(value, NULL, 10), found_crf = true;
  }
  (void)fclose(f); /* read-only stream */
  return (found_grain && found_var && found_crf) != 0;
}

bool vmav_save_cached_scores(const char *cache_path, double grain_score, double grain_variance,
                             int crf) {
  FILE *f = fopen(cache_path, "w");
  if (!f)
    return false;
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timestamp[32];
  (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ",
                 tm_info); /* fixed format always fits buffer */
  (void)fprintf(f,
                "{\n"
                "  \"generated\": \"%s\",\n"
                "  \"grain_score\": %.4f,\n"
                "  \"grain_variance\": %.4f,\n"
                "  \"crf\": %d\n"
                "}\n",
                timestamp, grain_score, grain_variance, crf);
  if (fclose(f) != 0)
    return false;
  return true;
}

/* ---- Track ordering helpers ---- */

int vmav_audio_lang_priority(const char *lang) {
  if (!lang || !lang[0])
    return 99;
  if (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0)
    return 0;
  if (strcmp(lang, "eng") == 0)
    return 1;
  return 50;
}

int vmav_cmp_audio_order(const void *a, const void *b) {
  const TrackInfo *ta = a;
  const TrackInfo *tb = b;
  return vmav_audio_lang_priority(ta->language) - vmav_audio_lang_priority(tb->language);
}

int vmav_sub_sort_key(const char *lang, int is_forced) {
  if (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0)
    return is_forced ? 0 : 10;
  if (strcmp(lang, "eng") == 0)
    return 20;
  return 50;
}

/**
 * @brief Dump every relevant encoder knob from the resolved preset.
 *
 * Called by --grain-only so the user can sanity-check what each tier
 * actually configures without committing to a multi-hour encode.
 * Focuses on the knobs that meaningfully affect grain pass-through and
 * perceived quality — preset / tune / ac-bias, the three filters
 * (TF / CDEF / restoration), variance boost, QMs, the noise vs
 * film-grain mechanism choice. Skips the rate-control plumbing
 * (already in the Plan section) and per-temporal-layer offsets.
 */
void vmav_print_encoder_knobs(const EncodePreset *p, int film_grain) {
  ui_section("Encoder knobs");

  ui_kv("Speed", "preset %d, keyint %d", p->preset, p->keyint);
  ui_kv("Tune", "%d  (0=VQ, 1=PSNR, 5=Film Grain)", p->tune);
  ui_kv("ac-bias", "%.1f", p->ac_bias);
  ui_kv("Sharpness", "%d", p->sharpness);
  ui_kv("Complex HVS", "%s", p->complex_hvs ? "on" : "off");

  /* Grain mechanism — selected per-preset, plus the dynamic level. */
  if (p->use_noise) {
    char size_buf[32];
    if (p->noise_size < 0)
      snprintf(size_buf, sizeof(size_buf), "auto");
    else
      snprintf(size_buf, sizeof(size_buf), "%d", p->noise_size);
    char chroma_buf[64];
    if (p->noise_chroma_strength < 0)
      snprintf(chroma_buf, sizeof(chroma_buf), "auto (~60%% of luma)");
    else if (p->noise_chroma_strength == 0)
      snprintf(chroma_buf, sizeof(chroma_buf), "off");
    else
      snprintf(chroma_buf, sizeof(chroma_buf), "%d", p->noise_chroma_strength);
    ui_kv("Grain mech", "%s", "--noise (synthetic overlay, 4.1.0+)");
    ui_kv("Strength", "%d", film_grain);
    ui_kv("Noise size", "%s", size_buf);
    ui_kv("Noise chroma", "%s", chroma_buf);
    ui_kv("Chroma← luma", "%s", p->noise_chroma_from_luma ? "on" : "off");
  } else {
    ui_kv("Grain mech", "%s", "--film-grain (analyzed) + denoise=1");
    ui_kv("Strength", "%d", film_grain);
  }

  /* Filters that affect grain pass-through. */
  if (p->enable_tf)
    ui_kv("Temporal flt", "on  (frame %d, keyframe %d)", p->tf_strength, p->kf_tf_strength);
  else
    ui_kv("Temporal flt", "off");
  const char *dlf_if_nonzero = p->enable_dlf == 2 ? "accurate" : "on";
  ui_kv("DLF", "%s", p->enable_dlf == 0 ? "off" : dlf_if_nonzero);
  ui_kv("CDEF scaling", "%d", p->cdef_scaling);
  const char *restoration_if_nonzero = p->enable_restoration == 1 ? "on" : "default";
  ui_kv("Restoration", "%s", p->enable_restoration == 0 ? "off" : restoration_if_nonzero);
  ui_kv("Noise-adapt", "%d  (0=off, 2=tune-default, 3=CDEF-only, 4=restoration-only)",
        p->noise_adaptive_filtering);

  /* Variance boost (HDR-relevant). */
  ui_kv("Var boost", "strength %d, octile %d, curve %d", p->variance_boost, p->variance_octile,
        p->variance_curve);

  /* Quantization matrices. */
  if (p->enable_qm == 1) {
    char qm_buf[32];
    if (p->qm_min >= 0 && p->qm_max >= 0)
      snprintf(qm_buf, sizeof(qm_buf), "luma %d-%d", p->qm_min, p->qm_max);
    else
      snprintf(qm_buf, sizeof(qm_buf), "default range");
    char chroma_qm_buf[32];
    if (p->chroma_qm_min >= 0 && p->chroma_qm_max >= 0)
      snprintf(chroma_qm_buf, sizeof(chroma_qm_buf), "chroma %d-%d", p->chroma_qm_min,
               p->chroma_qm_max);
    else
      snprintf(chroma_qm_buf, sizeof(chroma_qm_buf), "chroma default");
    ui_kv("Quant matrix", "%s, %s", qm_buf, chroma_qm_buf);
  } else {
    ui_kv("Quant matrix", "off");
  }

  /* Other meaningful knobs. */
  ui_kv("HBD MDS", "%d  (0=default, 1=full 10b, 2=hybrid)", p->hbd_mds);
  ui_kv("Max tx size", "%d", p->max_tx_size);
  ui_kv("QP scale comp", "%.1f", p->qp_scale_compress_strength);

  /* Rate control extras the Plan block doesn't cover. */
  if (p->look_ahead_distance >= 0)
    ui_kv("Look-ahead", "%d frames", p->look_ahead_distance);
  if (p->vbr_max_section_pct > 0)
    ui_kv("VBR max-sec", "%d%%", p->vbr_max_section_pct);
  if (p->gop_constraint_rc == 1)
    ui_kv("GOP RC match", "on");
}
