/**
 * @file crf_search.c
 * @brief Orchestrator: cut samples, encode at adaptive CRFs, score, converge.
 *
 * Strategy:
 *   1. Cut N short sample clips from the source at evenly spaced offsets
 *      (`ffmpeg -ss -t -c copy`, snaps to nearest keyframe), then transcode
 *      each to lossless FFV1 so the encoder and scorer see identical pixels.
 *   2. Probe an initial CRF (30), then a second CRF that brackets the target.
 *      If both fall on the same side of target, extend the bracket (saturation
 *      recovery). Then run interpolated bisection until the bracket is narrow
 *      enough or the probe budget is exhausted.
 *   3. VMAF is the search-driving metric (PCC ~0.90 vs MOS on AV1 4K per
 *      arxiv 2511.00969). XPSNR is computed only on the chosen probe as an
 *      informational sanity check.
 *
 * Dolby Vision RPU is intentionally NOT attached during the search: RPU is
 * metadata that does not affect AV1 coding quality. The real encode uses RPU.
 */

#include "crf_search.h"

#include "video_encode.h"

#include <cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ====================================================================== */
/*  Constants                                                             */
/* ====================================================================== */

#define CRF_SEARCH_MIN 10
#define CRF_SEARCH_MAX 60
#define CRF_SEARCH_INITIAL 30
#define CRF_SEARCH_BRACKET_MIN_WIDTH 2

/* ====================================================================== */
/*  Helpers                                                               */
/* ====================================================================== */

static int file_exists_nonempty(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && st.st_size > 0;
}

static int clamp_int(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

/**
 * @brief Score a pair of files with VMAF via system ffmpeg.
 *
 * Runs `ffmpeg -lavfi libvmaf`, writes a JSON log, parses per-frame VMAF
 * scores with cJSON, and returns the p10 (10th-percentile).
 *
 * @return p10 on success, or -1.0 on failure.
 */
static double score_vmaf_p10(const char *ref_path, const char *dis_path,
                             const char *json_path) {
  /* Run VMAF via system ffmpeg. distorted = first input, ref = second. */
  char cmd[4096];
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -y -loglevel error -nostdin "
           "-i \"%s\" -i \"%s\" "
           "-lavfi libvmaf=log_fmt=json:log_path=\"%s\":n_threads=4 "
           "-f null -",
           dis_path, ref_path, json_path);
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "  CRF search: VMAF scoring failed (rc=%d)\n", rc);
    return -1.0;
  }

  FILE *fp = fopen(json_path, "rb");
  if (!fp) {
    fprintf(stderr, "  CRF search: cannot open VMAF log %s\n", json_path);
    return -1.0;
  }
  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (fsize <= 0 || fsize > 100 * 1024 * 1024) {
    fclose(fp);
    return -1.0;
  }
  char *json_buf = (char *)malloc((size_t)fsize + 1);
  if (!json_buf) {
    fclose(fp);
    return -1.0;
  }
  fread(json_buf, 1, (size_t)fsize, fp);
  json_buf[fsize] = '\0';
  fclose(fp);

  cJSON *root = cJSON_Parse(json_buf);
  free(json_buf);
  if (!root) {
    fprintf(stderr, "  CRF search: VMAF JSON parse failed\n");
    return -1.0;
  }

  cJSON *frames = cJSON_GetObjectItemCaseSensitive(root, "frames");
  if (!cJSON_IsArray(frames)) {
    cJSON_Delete(root);
    return -1.0;
  }

  int n = cJSON_GetArraySize(frames);
  if (n <= 0) {
    cJSON_Delete(root);
    return -1.0;
  }

  double *scores = (double *)malloc((size_t)n * sizeof(double));
  if (!scores) {
    cJSON_Delete(root);
    return -1.0;
  }

  int count = 0;
  cJSON *frame = NULL;
  cJSON_ArrayForEach(frame, frames) {
    cJSON *metrics = cJSON_GetObjectItemCaseSensitive(frame, "metrics");
    if (!metrics)
      continue;
    cJSON *vmaf = cJSON_GetObjectItemCaseSensitive(metrics, "vmaf");
    if (cJSON_IsNumber(vmaf))
      scores[count++] = vmaf->valuedouble;
  }
  cJSON_Delete(root);

  if (count == 0) {
    free(scores);
    return -1.0;
  }

  /* Sort ascending to compute p10. */
  for (int i = 0; i < count - 1; i++)
    for (int j = i + 1; j < count; j++)
      if (scores[j] < scores[i]) {
        double tmp = scores[i];
        scores[i] = scores[j];
        scores[j] = tmp;
      }

  int p10_idx = count / 10;
  double p10 = scores[p10_idx];
  free(scores);
  return p10;
}

/**
 * @brief Score a pair of files with XPSNR via system ffmpeg.
 *
 * Stats output is one line per frame:
 *   `n:    1  XPSNR y: 38.50  XPSNR u: 41.10  XPSNR v: 40.55`
 * (`inf` for identical frames). Returns the p10 of the per-frame composite
 * `(4·Y + U + V) / 6` in dB — the community-standard weighting used by
 * SVT-AV1-PSY tooling. Capped per-channel at 60 dB before combining (the
 * `inf` case for identical blocks).
 *
 * Per ab-av1 issue #258, both inputs MUST be opened with explicit `-r <fps>`
 * to prevent ffmpeg's filter graph from desyncing on VFR input — without
 * this the score can swing by ~10 dB on identical content.
 *
 * @return p10 in dB on success, or -1.0 on failure.
 */
static double score_xpsnr_p10(const char *ref_path, const char *dis_path,
                              const char *stats_path, double fps) {
  if (fps <= 0.0)
    fps = 24.0;

  char cmd[4096];
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -y -loglevel error -nostdin "
           "-r %.6f -i \"%s\" -r %.6f -i \"%s\" "
           "-lavfi \"[0:v][1:v]xpsnr=stats_file=%s\" "
           "-f null -",
           fps, dis_path, fps, ref_path, stats_path);
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "  CRF search: XPSNR scoring failed (rc=%d)\n", rc);
    return -1.0;
  }

  FILE *fp = fopen(stats_path, "r");
  if (!fp) {
    fprintf(stderr, "  CRF search: cannot open XPSNR stats %s\n", stats_path);
    return -1.0;
  }

  /* First pass: count lines for allocation. */
  int cap = 256;
  double *scores = (double *)malloc((size_t)cap * sizeof(double));
  if (!scores) {
    fclose(fp);
    return -1.0;
  }
  int count = 0;

  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    /* Locate the three "XPSNR <ch>:" tokens; each may be a float or "inf". */
    double y = -1.0, u = -1.0, v = -1.0;
    const char *ty = strstr(line, "XPSNR y:");
    const char *tu = strstr(line, "XPSNR u:");
    const char *tv = strstr(line, "XPSNR v:");
    if (!ty || !tu || !tv)
      continue;
    if (sscanf(ty + 8, " %lf", &y) != 1) {
      if (strstr(ty + 8, "inf")) y = 60.0; else continue;
    }
    if (sscanf(tu + 8, " %lf", &u) != 1) {
      if (strstr(tu + 8, "inf")) u = 60.0; else continue;
    }
    if (sscanf(tv + 8, " %lf", &v) != 1) {
      if (strstr(tv + 8, "inf")) v = 60.0; else continue;
    }
    /* Cap each channel at 60 dB to keep "inf" frames from dominating. */
    if (y > 60.0 || !isfinite(y)) y = 60.0;
    if (u > 60.0 || !isfinite(u)) u = 60.0;
    if (v > 60.0 || !isfinite(v)) v = 60.0;

    /* Community-standard weighted composite: (4Y + U + V) / 6. */
    double composite = (4.0 * y + u + v) / 6.0;

    if (count == cap) {
      cap *= 2;
      double *grown = (double *)realloc(scores, (size_t)cap * sizeof(double));
      if (!grown) {
        free(scores);
        fclose(fp);
        return -1.0;
      }
      scores = grown;
    }
    scores[count++] = composite;
  }
  fclose(fp);

  if (count == 0) {
    free(scores);
    return -1.0;
  }

  for (int i = 0; i < count - 1; i++)
    for (int j = i + 1; j < count; j++)
      if (scores[j] < scores[i]) {
        double tmp = scores[i];
        scores[i] = scores[j];
        scores[j] = tmp;
      }

  int p10_idx = count / 10;
  double p10 = scores[p10_idx];
  free(scores);
  return p10;
}

/**
 * @brief Cut a short sample clip from @p src starting at @p offset_sec,
 *        duration @p duration_sec, via `ffmpeg -c copy`.
 *
 * Stream-copy snaps to the nearest keyframe at/after the offset, so the
 * actual clip may start up to one GOP later than requested. That's fine —
 * the same clip is used as both reference and encoder input, so timing
 * alignment is intrinsic.
 */
static int cut_sample(const char *src, const char *dst, int offset_sec,
                      int duration_sec) {
  if (file_exists_nonempty(dst))
    return 0;

  char cmd[4096];
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -y -loglevel fatal -nostdin -ss %d -i \"%s\" -t %d "
           "-map 0:v:0 -c copy -an -sn -dn \"%s\"",
           offset_sec, src, duration_sec, dst);
  int rc = system(cmd);
  if (rc != 0 || !file_exists_nonempty(dst)) {
    fprintf(stderr, "  CRF search: sample cut failed (rc=%d) for %s\n", rc,
            dst);
    return -1;
  }
  return 0;
}

/**
 * @brief Encode @p sample_path at @p crf into @p encoded_path using the
 *        project's real encode pipeline.
 *
 * Reuses the caller's preset / grain / HDR / info / crop, so the probe
 * encode matches a real encode as closely as possible aside from the
 * rate-control mode and the missing DV RPU.
 */
static int encode_sample_at_crf(const CrfSearchConfig *cfg,
                                const char *sample_path,
                                const char *encoded_path, int crf) {
  if (file_exists_nonempty(encoded_path))
    return 0;

  /* MediaInfo.duration from the full source overestimates the sample's
   * duration — encode_video only uses it for the progress ETA, not for
   * correctness, so passing the parent info is safe. */
  VideoEncodeConfig vcfg = {
      .input_path = sample_path,
      .output_path = encoded_path,
      .rpu_path = NULL, /* see file header */
      .preset = cfg->preset,
      .film_grain = 0, /* disable grain synthesis for scoring accuracy:
                         * SVT-AV1 film-grain denoises then resynthesises
                         * a *different* grain pattern; the metric sees the
                         * pattern mismatch as distortion → artificially low
                         * scores. With fg=0, the encoder preserves grain
                         * as-is and the metric compares like with like.
                         * The final encode still uses cfg->film_grain. */
      .grain_score = cfg->grain_score,
      .target_bitrate = 0,
      .crf = crf,
      .info = cfg->info,
      .crop = cfg->crop,
      .hdr = cfg->hdr,
  };
  VideoEncodeResult vr = encode_video(&vcfg);
  if (vr.error != 0) {
    fprintf(stderr, "  CRF search: encode failed at CRF %d (err=%d)\n", crf,
            vr.error);
    return vr.error;
  }
  if (vr.frames_encoded <= 0) {
    fprintf(stderr, "  CRF search: encode produced 0 frames at CRF %d\n", crf);
    return -1;
  }
  return 0;
}

/* ====================================================================== */
/*  Probe machinery                                                       */
/* ====================================================================== */

typedef struct {
  int crf;
  double p10;        /**< Mean across samples of per-sample VMAF p10. */
  double kbps;       /**< Mean across samples of per-sample bitrate. */
  int valid;         /**< Non-zero if this entry was successfully scored. */
} ProbePoint;

/**
 * @brief Encode every sample at @p crf, score with VMAF, aggregate to one point.
 *
 * Re-uses existing encoded-sample files on disk (deterministic naming), so
 * a CRF probed earlier in the run is essentially free to re-query.
 *
 * @return 0 on success (any sample scored), negative if no sample scored.
 */
static int probe_at_crf(const CrfSearchConfig *cfg,
                        char sample_paths[][1024],
                        char ref_paths[][1024],
                        int n_samples, int dur, int crf,
                        ProbePoint *out) {
  double sum_p10 = 0.0;
  double sum_kbps = 0.0;
  int scored = 0;

  for (int i = 0; i < n_samples; i++) {
    char enc_path[1024];
    snprintf(enc_path, sizeof(enc_path), "%s/crfsearch_sample_%d_crf%d.mkv",
             cfg->workdir, i, crf);

    if (encode_sample_at_crf(cfg, sample_paths[i], enc_path, crf) < 0)
      continue;

    struct stat enc_st;
    double this_kbps = 0.0;
    if (stat(enc_path, &enc_st) == 0 && enc_st.st_size > 0 && dur > 0)
      this_kbps = (double)enc_st.st_size * 8.0 / 1000.0 / (double)dur;

    char vmaf_json[1024];
    snprintf(vmaf_json, sizeof(vmaf_json),
             "%s/crfsearch_sample_%d_crf%d_vmaf.json",
             cfg->workdir, i, crf);
    double this_p10 = score_vmaf_p10(ref_paths[i], enc_path, vmaf_json);
    if (this_p10 < 0.0)
      continue;

    sum_p10 += this_p10;
    sum_kbps += this_kbps;
    scored++;
  }

  if (scored == 0) {
    out->valid = 0;
    return -1;
  }
  out->crf = crf;
  out->p10 = sum_p10 / scored;
  out->kbps = sum_kbps / scored;
  out->valid = 1;
  return 0;
}

/**
 * @brief Score XPSNR on the first sample of @p crf as an informational check.
 */
static double xpsnr_for_crf(const CrfSearchConfig *cfg,
                            char ref_paths[][1024],
                            int crf, double fps) {
  char enc_path[1024];
  snprintf(enc_path, sizeof(enc_path), "%s/crfsearch_sample_%d_crf%d.mkv",
           cfg->workdir, 0, crf);
  if (!file_exists_nonempty(enc_path))
    return -1.0;
  char stats_path[1024];
  snprintf(stats_path, sizeof(stats_path),
           "%s/crfsearch_sample_%d_crf%d_xpsnr.log", cfg->workdir, 0, crf);
  return score_xpsnr_p10(ref_paths[0], enc_path, stats_path, fps);
}

static void print_probe_row(const ProbePoint *p, int probe_idx, int max_probes,
                            time_t search_start) {
  int probe_sec = (int)difftime(time(NULL), search_start);
  char eta_str[32] = "";
  if (probe_idx > 0 && probe_idx < max_probes) {
    int avg_per_probe = probe_sec / probe_idx;
    int eta_sec = avg_per_probe * (max_probes - probe_idx);
    if (eta_sec > 0)
      snprintf(eta_str, sizeof(eta_str), "ETA %02d:%02d",
               eta_sec / 60, eta_sec % 60);
  }
  printf("  CRF %-3d  %7.2f  %9.0f  %3d:%02d  %s\n",
         p->crf, p->p10, p->kbps,
         probe_sec / 60, probe_sec % 60, eta_str);
  fflush(stdout);
}

/* ====================================================================== */
/*  Public API                                                            */
/* ====================================================================== */

CrfSearchResult crf_search_run(const CrfSearchConfig *cfg) {
  CrfSearchResult result = {0};

  if (!cfg || !cfg->input_path || !cfg->workdir || !cfg->info || !cfg->preset) {
    result.error = -1;
    return result;
  }

  int n_samples = cfg->sample_count > 0 ? cfg->sample_count : 2;
  int dur = cfg->sample_duration > 0 ? cfg->sample_duration : 10;
  int max_probes = cfg->max_probes > 0 ? cfg->max_probes : 8;
  if (max_probes > 16) max_probes = 16;

  /* Grain-aware VMAF target — preset-dependent:
   *
   * Digital/live-action: film_grain synthesis will re-add grain in the final
   *   encode, so VMAF overestimates the perceptual gap → soften the target.
   *
   * Analog (Super 35 / IMAX film): the grain IS the artistic content.
   *   film_grain synthesis preserves it faithfully, but VMAF still needs
   *   to see the grain held accurately → don't soften (or soften less).
   *
   * Animation: zero real grain, high scores are texture/dithering artifacts.
   *   No grain will be re-synthesized → never soften the target. */
  double effective_target = cfg->target_p10;
  switch (cfg->quality) {
  case QUALITY_LIVEACTION:
  case QUALITY_SUPER35_DIGITAL:
  case QUALITY_IMAX_DIGITAL:
    if (cfg->grain_score > 0.20)
      effective_target -= 2.0; /* heavy grain: e.g. 93 -> 91 */
    else if (cfg->grain_score > 0.10)
      effective_target -= 1.0; /* moderate grain: e.g. 93 -> 92 */
    break;
  case QUALITY_SUPER35_ANALOG:
  case QUALITY_IMAX_ANALOG:
    if (cfg->grain_score > 0.20)
      effective_target -= 1.0; /* real film grain — soften gently */
    break;
  case QUALITY_ANIMATION:
  default:
    /* No softening — detector sees texture, not grain. */
    break;
  }

  double src_dur = cfg->info->duration;
  if (src_dur < (double)(dur * 2)) {
    fprintf(stderr,
            "  CRF search: source too short (%.1fs) for %d samples x %ds\n",
            src_dur, n_samples, dur);
    result.error = -1;
    return result;
  }

  /* Sample offsets — evenly spaced, skipping first/last 5%. */
  int offsets[8] = {0};
  if (n_samples > 8) n_samples = 8;
  double usable_start = src_dur * 0.05;
  double usable_end = src_dur * 0.95 - dur;
  if (usable_end <= usable_start)
    usable_end = usable_start + dur;
  for (int i = 0; i < n_samples; i++) {
    double frac = (n_samples == 1) ? 0.5 : (double)(i + 1) / (n_samples + 1);
    offsets[i] = (int)(usable_start + frac * (usable_end - usable_start));
  }

  char sample_paths[8][1024];
  char ref_paths[8][1024];

  printf("\nOptimal bitrate search (target VMAF p10 >= %.2f", cfg->target_p10);
  if (effective_target != cfg->target_p10)
    printf(", adjusted to %.2f for grain=%.3f", effective_target,
           cfg->grain_score);
  printf(", max %d probes)\n", max_probes);
  printf("  Preparing %d samples (%ds each)...\n", n_samples, dur);

  /* Build crop filter string if crop is active. encode_video() applies crop
   * internally, so the encoded output will be smaller than the raw sample.
   * We must create a cropped reference at the same resolution for scoring. */
  int has_crop = (cfg->crop && cfg->crop->error == 0 &&
                  (cfg->crop->top || cfg->crop->bottom ||
                   cfg->crop->left || cfg->crop->right));
  char crop_filter[256] = "";
  if (has_crop) {
    int cw = cfg->info->width - cfg->crop->left - cfg->crop->right;
    int ch = cfg->info->height - cfg->crop->top - cfg->crop->bottom;
    cw &= ~1;
    ch &= ~1;
    snprintf(crop_filter, sizeof(crop_filter),
             "crop=%d:%d:%d:%d", cw, ch, cfg->crop->left, cfg->crop->top);
  }

  for (int i = 0; i < n_samples; i++) {
    char raw_path[1024];
    snprintf(raw_path, sizeof(raw_path),
             "%s/crfsearch_sample_%d.mkv", cfg->workdir, i);
    if (cut_sample(cfg->input_path, raw_path, offsets[i], dur) < 0) {
      result.error = -1;
      return result;
    }

    /* Transcode the stream-copied sample to lossless FFV1. This decodes
     * the source codec exactly once, eliminating non-deterministic error
     * concealment on broken reference frames. */
    snprintf(sample_paths[i], sizeof(sample_paths[i]),
             "%s/crfsearch_sample_%d.ffv1.mkv", cfg->workdir, i);
    if (!file_exists_nonempty(sample_paths[i])) {
      char cmd[4096];
      snprintf(cmd, sizeof(cmd),
               "ffmpeg -y -loglevel error -nostdin -i \"%s\" "
               "-c:v ffv1 -level 3 -pix_fmt yuv420p10le "
               "-an -sn -dn \"%s\"",
               raw_path, sample_paths[i]);
      int rc = system(cmd);
      if (rc != 0 || !file_exists_nonempty(sample_paths[i])) {
        fprintf(stderr,
                "  CRF search: FFV1 transcode failed (rc=%d)\n", rc);
        result.error = -1;
        return result;
      }
    }

    if (has_crop) {
      snprintf(ref_paths[i], sizeof(ref_paths[i]),
               "%s/crfsearch_sample_%d_ref.ffv1.mkv", cfg->workdir, i);
      if (!file_exists_nonempty(ref_paths[i])) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -loglevel error -nostdin -i \"%s\" "
                 "-vf \"%s\" -c:v ffv1 -level 3 -pix_fmt yuv420p10le "
                 "-an -sn -dn \"%s\"",
                 sample_paths[i], crop_filter, ref_paths[i]);
        int rc = system(cmd);
        if (rc != 0 || !file_exists_nonempty(ref_paths[i])) {
          fprintf(stderr,
                  "  CRF search: cropped ref creation failed (rc=%d)\n", rc);
          result.error = -1;
          return result;
        }
      }
    } else {
      snprintf(ref_paths[i], sizeof(ref_paths[i]), "%s", sample_paths[i]);
    }
  }

  printf("\n  %-7s  %7s  %9s  %7s\n",
         "CRF", "p10", "kbps", "time");
  printf("  %-7s  %7s  %9s  %7s\n",
         "-------", "-------", "---------", "-------");
  fflush(stdout);

  /* ---- Adaptive search ---- */
  ProbePoint probes[16] = {0};
  int n_probes = 0;
  time_t search_start = time(NULL);

  /* Phase A: probe initial CRF, then a second to bracket the target. */
  ProbePoint p0;
  if (probe_at_crf(cfg, sample_paths, ref_paths, n_samples, dur,
                   CRF_SEARCH_INITIAL, &p0) < 0) {
    result.error = -1;
    return result;
  }
  probes[n_probes++] = p0;
  result.probes_succeeded++;
  print_probe_row(&p0, n_probes, max_probes, search_start);

  int second_crf = (p0.p10 >= effective_target) ? 45 : 20;
  ProbePoint p1;
  if (probe_at_crf(cfg, sample_paths, ref_paths, n_samples, dur, second_crf,
                   &p1) == 0) {
    probes[n_probes++] = p1;
    result.probes_succeeded++;
    print_probe_row(&p1, n_probes, max_probes, search_start);
  }

  /* Sort probes by CRF ascending (so lower index = lower CRF = higher p10). */
  for (int i = 0; i < n_probes - 1; i++)
    for (int j = i + 1; j < n_probes; j++)
      if (probes[j].crf < probes[i].crf) {
        ProbePoint t = probes[i]; probes[i] = probes[j]; probes[j] = t;
      }

  /* Phase C: saturation recovery — extend bracket until target is bracketed
   * or we hit search-range edges / probe budget. */
  while (n_probes < max_probes) {
    int lo_crf = probes[0].crf;
    int hi_crf = probes[n_probes - 1].crf;
    double lo_p10 = probes[0].p10;
    double hi_p10 = probes[n_probes - 1].p10;

    int next_crf = -1;
    if (lo_p10 < effective_target && lo_crf > CRF_SEARCH_MIN) {
      /* Even the lowest CRF probe falls short — go lower. */
      next_crf = clamp_int(lo_crf - 5, CRF_SEARCH_MIN, lo_crf - 1);
    } else if (hi_p10 >= effective_target && hi_crf < CRF_SEARCH_MAX) {
      /* Even the highest CRF probe still meets target — go higher. */
      next_crf = clamp_int(hi_crf + 10, hi_crf + 1, CRF_SEARCH_MAX);
    } else {
      break; /* either bracketed or saturated at search edge */
    }

    ProbePoint pp;
    if (probe_at_crf(cfg, sample_paths, ref_paths, n_samples, dur, next_crf,
                     &pp) == 0) {
      /* Insert keeping CRF-sorted order. */
      int ins = n_probes;
      while (ins > 0 && probes[ins - 1].crf > pp.crf) {
        probes[ins] = probes[ins - 1];
        ins--;
      }
      probes[ins] = pp;
      n_probes++;
      result.probes_succeeded++;
      print_probe_row(&pp, n_probes, max_probes, search_start);
    } else {
      break;
    }
  }

  /* Phase B: interpolated bisection within the bracket. */
  while (n_probes < max_probes) {
    /* Find the bracketing pair (probes are CRF-sorted, p10 monotone-decreasing). */
    int lo_idx = -1, hi_idx = -1;
    for (int i = 0; i < n_probes - 1; i++) {
      if (probes[i].p10 >= effective_target &&
          probes[i + 1].p10 < effective_target) {
        lo_idx = i;
        hi_idx = i + 1;
        break;
      }
    }
    if (lo_idx < 0)
      break; /* No bracket — saturation case, handled in solve below. */

    int lo_crf = probes[lo_idx].crf;
    int hi_crf = probes[hi_idx].crf;
    if (hi_crf - lo_crf <= CRF_SEARCH_BRACKET_MIN_WIDTH)
      break;

    double span = probes[lo_idx].p10 - probes[hi_idx].p10;
    int next_crf;
    if (span < 0.1) {
      next_crf = (lo_crf + hi_crf) / 2;
    } else {
      double t = (probes[lo_idx].p10 - effective_target) / span;
      next_crf = lo_crf + (int)(t * (hi_crf - lo_crf) + 0.5);
    }
    /* Keep the probe strictly inside the bracket. */
    if (next_crf <= lo_crf) next_crf = lo_crf + 1;
    if (next_crf >= hi_crf) next_crf = hi_crf - 1;

    /* Skip if already probed (shouldn't happen given the strict-inside guard,
     * but defend against pathological cases). */
    int dup = 0;
    for (int i = 0; i < n_probes; i++)
      if (probes[i].crf == next_crf) { dup = 1; break; }
    if (dup) break;

    ProbePoint pp;
    if (probe_at_crf(cfg, sample_paths, ref_paths, n_samples, dur, next_crf,
                     &pp) < 0)
      break;

    int ins = n_probes;
    while (ins > 0 && probes[ins - 1].crf > pp.crf) {
      probes[ins] = probes[ins - 1];
      ins--;
    }
    probes[ins] = pp;
    n_probes++;
    result.probes_succeeded++;
    print_probe_row(&pp, n_probes, max_probes, search_start);
  }

  /* ---- Solve for recommended CRF ---- */
  int rec_crf = -1;
  double rec_p10 = 0.0;
  double rec_kbps = 0.0;
  int saturated = 0;

  /* Find bracket. */
  int lo_idx = -1, hi_idx = -1;
  for (int i = 0; i < n_probes - 1; i++) {
    if (probes[i].p10 >= effective_target &&
        probes[i + 1].p10 < effective_target) {
      lo_idx = i;
      hi_idx = i + 1;
      break;
    }
  }

  if (lo_idx >= 0) {
    /* Interpolate within bracket. */
    int lo_crf = probes[lo_idx].crf;
    int hi_crf = probes[hi_idx].crf;
    double span = probes[lo_idx].p10 - probes[hi_idx].p10;
    if (span < 0.1) {
      rec_crf = lo_crf;
      rec_p10 = probes[lo_idx].p10;
      rec_kbps = probes[lo_idx].kbps;
    } else {
      double t = (probes[lo_idx].p10 - effective_target) / span;
      rec_crf = lo_crf + (int)(t * (hi_crf - lo_crf) + 0.5);
      rec_p10 = probes[lo_idx].p10 +
                t * (probes[hi_idx].p10 - probes[lo_idx].p10);
      rec_kbps = probes[lo_idx].kbps +
                 ((double)(rec_crf - lo_crf) / (double)(hi_crf - lo_crf)) *
                     (probes[hi_idx].kbps - probes[lo_idx].kbps);
    }
  } else {
    /* No bracket: target is outside the probed range. Clamp. */
    saturated = 1;
    if (probes[0].p10 < effective_target) {
      /* Target unreachable — even lowest CRF can't meet it. */
      rec_crf = probes[0].crf;
      rec_p10 = probes[0].p10;
      rec_kbps = probes[0].kbps;
      fprintf(stderr,
              "\n  WARNING: target VMAF %.2f exceeds encoder capability on "
              "this content (best probe = %.2f at CRF %d). Using lowest CRF.\n",
              effective_target, probes[0].p10, probes[0].crf);
    } else {
      /* Content is easy — even highest CRF beats target. */
      rec_crf = probes[n_probes - 1].crf;
      rec_p10 = probes[n_probes - 1].p10;
      rec_kbps = probes[n_probes - 1].kbps;
      printf(
          "\n  Note: content was easy to encode — even CRF %d still scores "
          "%.2f (above target %.2f). Using highest CRF.\n",
          probes[n_probes - 1].crf, probes[n_probes - 1].p10,
          effective_target);
    }
  }

  /* ---- Informational XPSNR on closest probe ---- */
  int closest_idx = 0;
  int best_diff = 100;
  for (int i = 0; i < n_probes; i++) {
    int d = abs(probes[i].crf - rec_crf);
    if (d < best_diff) {
      best_diff = d;
      closest_idx = i;
    }
  }
  double xpsnr_p10 = xpsnr_for_crf(cfg, ref_paths, probes[closest_idx].crf,
                                   cfg->info->framerate);

  result.recommended_crf = rec_crf;
  result.predicted_p10 = rec_p10;
  result.measured_bitrate_kbps = (int)(rec_kbps + 0.5);
  result.xpsnr_p10 = xpsnr_p10;
  result.saturated = saturated;

  int total_sec = (int)difftime(time(NULL), search_start);
  printf("  %-7s  %7s  %9s  %7s\n",
         "-------", "-------", "---------", "-------");
  if (xpsnr_p10 > 0.0) {
    printf("\n  Result: CRF %d -> %d kbps (VMAF p10=%.2f, XPSNR p10=%.2f dB) "
           "in %02d:%02d, %d probes\n",
           rec_crf, result.measured_bitrate_kbps, rec_p10, xpsnr_p10,
           total_sec / 60, total_sec % 60, n_probes);
  } else {
    printf("\n  Result: CRF %d -> %d kbps (VMAF p10=%.2f) in %02d:%02d, "
           "%d probes\n",
           rec_crf, result.measured_bitrate_kbps, rec_p10,
           total_sec / 60, total_sec % 60, n_probes);
  }

  return result;
}
