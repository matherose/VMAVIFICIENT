/**
 * @file crf_search.c
 * @brief Minimal ab-av1-style CRF search.
 *
 * Strategy:
 *   1. Cut N short sample clips from the source at evenly spaced offsets
 *      (`ffmpeg -ss -t -c copy`, snaps to nearest keyframe), then transcode
 *      each to lossless FFV1 so the encoder and scorer see identical pixels.
 *   2. Probe an initial CRF (30). Seed a second probe on the other side of
 *      the target (22 if below, 38 if above). Interpolated bisection from
 *      there until the bracket closes or the probe budget is exhausted.
 *   3. Score with MEAN VMAF — p10 is grain-hostile and demands unreachable
 *      tail quality on heavy-grain content. ab-av1 uses mean for the same
 *      reason.
 *
 * Probe encodes use the REAL film_grain setting — not fg=0. Grain synthesis
 * changes bitrate by 3-5× on heavy-grain films, so probing at fg=0 makes the
 * measured bitrate useless as a VBR target. The VMAF-vs-grain artifact the
 * old fg=0 code tried to dodge is already handled by switching from p10 to
 * mean (and by having the same grain treatment in probe and final encode).
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
#define CRF_SEARCH_BRACKET_LOW 22
#define CRF_SEARCH_BRACKET_HIGH 38

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
 * scores with cJSON, and returns the arithmetic mean.
 *
 * @return mean VMAF on success, or -1.0 on failure.
 */
static double score_vmaf_mean(const char *ref_path, const char *dis_path,
                              const char *json_path) {
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

  double sum = 0.0;
  int count = 0;
  cJSON *frame = NULL;
  cJSON_ArrayForEach(frame, frames) {
    cJSON *metrics = cJSON_GetObjectItemCaseSensitive(frame, "metrics");
    if (!metrics)
      continue;
    cJSON *vmaf = cJSON_GetObjectItemCaseSensitive(metrics, "vmaf");
    if (cJSON_IsNumber(vmaf)) {
      sum += vmaf->valuedouble;
      count++;
    }
  }
  cJSON_Delete(root);

  if (count == 0)
    return -1.0;
  return sum / (double)count;
}

/**
 * @brief Cut a short sample clip via `ffmpeg -c copy`.
 *
 * Stream-copy snaps to the nearest keyframe at/after the offset, so the
 * actual clip may start up to one GOP later than requested. That's fine —
 * the same clip is used as both reference and encoder input.
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
 * @brief Encode @p sample_path at @p crf using the project's real encode
 *        pipeline with the caller's real film_grain setting.
 *
 * Probe and final encode use the SAME grain synthesis — so the measured
 * bitrate is directly usable as the final VBR target.
 */
static int encode_sample_at_crf(const CrfSearchConfig *cfg,
                                const char *sample_path,
                                const char *encoded_path, int crf) {
  if (file_exists_nonempty(encoded_path))
    return 0;

  VideoEncodeConfig vcfg = {
      .input_path = sample_path,
      .output_path = encoded_path,
      .rpu_path = NULL, /* see file header */
      .preset = cfg->preset,
      .film_grain = cfg->film_grain,
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
  double vmaf;       /**< Mean across samples of per-sample mean VMAF. */
  double kbps;       /**< Mean across samples of per-sample bitrate. */
  int valid;         /**< Non-zero if this entry was successfully scored. */
} ProbePoint;

/**
 * @brief Encode every sample at @p crf, score with mean VMAF, aggregate.
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
  double sum_vmaf = 0.0;
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
    double this_vmaf = score_vmaf_mean(ref_paths[i], enc_path, vmaf_json);
    if (this_vmaf < 0.0)
      continue;

    sum_vmaf += this_vmaf;
    sum_kbps += this_kbps;
    scored++;
  }

  if (scored == 0) {
    out->valid = 0;
    return -1;
  }
  out->crf = crf;
  out->vmaf = sum_vmaf / scored;
  out->kbps = sum_kbps / scored;
  out->valid = 1;
  return 0;
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
         p->crf, p->vmaf, p->kbps,
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

  int n_samples = cfg->sample_count > 0 ? cfg->sample_count : 3;
  int dur = cfg->sample_duration > 0 ? cfg->sample_duration : 15;
  int max_probes = cfg->max_probes > 0 ? cfg->max_probes : 6;
  if (max_probes > 16) max_probes = 16;
  double target = cfg->target_vmaf_mean;

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

  printf("\nOptimal bitrate search (target mean VMAF >= %.2f, %d samples × %ds, "
         "max %d probes)\n",
         target, n_samples, dur, max_probes);
  printf("  Preparing %d samples...\n", n_samples);

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
         "CRF", "VMAF", "kbps", "time");
  printf("  %-7s  %7s  %9s  %7s\n",
         "-------", "-------", "---------", "-------");
  fflush(stdout);

  /* ---- Adaptive search ---- */
  ProbePoint probes[16] = {0};
  int n_probes = 0;
  time_t search_start = time(NULL);

  /* Tolerance ladder — accept early convergence when the VMAF curve is flat.
   * Matches ab-av1: 0.1, 0.2, 0.4, 0.8... */
  double tolerance = 0.1;

  /* Seed probe #1 at CRF 30. */
  ProbePoint p0;
  if (probe_at_crf(cfg, sample_paths, ref_paths, n_samples, dur,
                   CRF_SEARCH_INITIAL, &p0) < 0) {
    result.error = -1;
    return result;
  }
  probes[n_probes++] = p0;
  result.probes_succeeded++;
  print_probe_row(&p0, n_probes, max_probes, search_start);

  /* Seed probe #2 on the far side of target. */
  int second_crf = (p0.vmaf >= target) ? CRF_SEARCH_BRACKET_HIGH
                                       : CRF_SEARCH_BRACKET_LOW;
  ProbePoint p1;
  if (probe_at_crf(cfg, sample_paths, ref_paths, n_samples, dur, second_crf,
                   &p1) == 0) {
    probes[n_probes++] = p1;
    result.probes_succeeded++;
    print_probe_row(&p1, n_probes, max_probes, search_start);
  }

  /* Keep probes CRF-sorted (ascending CRF = descending VMAF). */
  for (int i = 0; i < n_probes - 1; i++)
    for (int j = i + 1; j < n_probes; j++)
      if (probes[j].crf < probes[i].crf) {
        ProbePoint t = probes[i]; probes[i] = probes[j]; probes[j] = t;
      }

  /* Interpolated bisection. */
  while (n_probes < max_probes) {
    /* Find the bracketing pair. */
    int lo_idx = -1, hi_idx = -1;
    for (int i = 0; i < n_probes - 1; i++) {
      if (probes[i].vmaf >= target && probes[i + 1].vmaf < target) {
        lo_idx = i;
        hi_idx = i + 1;
        break;
      }
    }

    int next_crf;
    if (lo_idx < 0) {
      /* Not yet bracketed — extend on whichever side still has slack. */
      int lo_crf = probes[0].crf;
      int hi_crf = probes[n_probes - 1].crf;
      if (probes[0].vmaf < target && lo_crf > CRF_SEARCH_MIN) {
        next_crf = clamp_int(lo_crf - 5, CRF_SEARCH_MIN, lo_crf - 1);
      } else if (probes[n_probes - 1].vmaf >= target && hi_crf < CRF_SEARCH_MAX) {
        next_crf = clamp_int(hi_crf + 5, hi_crf + 1, CRF_SEARCH_MAX);
      } else {
        break; /* saturated at range edge */
      }
    } else {
      int lo_crf = probes[lo_idx].crf;
      int hi_crf = probes[hi_idx].crf;
      if (hi_crf - lo_crf <= 1)
        break; /* bracket closed */

      /* Accept early if either bound is within tolerance. */
      if (probes[lo_idx].vmaf - target <= tolerance ||
          target - probes[hi_idx].vmaf <= tolerance)
        break;

      double span = probes[lo_idx].vmaf - probes[hi_idx].vmaf;
      if (span < 0.1) {
        next_crf = (lo_crf + hi_crf) / 2;
      } else {
        double t = (probes[lo_idx].vmaf - target) / span;
        next_crf = lo_crf + (int)(t * (hi_crf - lo_crf) + 0.5);
      }
      if (next_crf <= lo_crf) next_crf = lo_crf + 1;
      if (next_crf >= hi_crf) next_crf = hi_crf - 1;
    }

    /* Skip if already probed. */
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

    tolerance *= 2.0;
  }

  /* ---- Solve for recommended CRF ---- */
  int rec_crf;
  double rec_vmaf;
  double rec_kbps;

  int lo_idx = -1, hi_idx = -1;
  for (int i = 0; i < n_probes - 1; i++) {
    if (probes[i].vmaf >= target && probes[i + 1].vmaf < target) {
      lo_idx = i;
      hi_idx = i + 1;
      break;
    }
  }

  if (lo_idx >= 0) {
    int lo_crf = probes[lo_idx].crf;
    int hi_crf = probes[hi_idx].crf;
    double span = probes[lo_idx].vmaf - probes[hi_idx].vmaf;
    if (span < 0.1 || hi_crf - lo_crf <= 1) {
      rec_crf = lo_crf;
      rec_vmaf = probes[lo_idx].vmaf;
      rec_kbps = probes[lo_idx].kbps;
    } else {
      double t = (probes[lo_idx].vmaf - target) / span;
      rec_crf = lo_crf + (int)(t * (hi_crf - lo_crf) + 0.5);
      if (rec_crf < lo_crf) rec_crf = lo_crf;
      if (rec_crf > hi_crf) rec_crf = hi_crf;
      rec_vmaf = probes[lo_idx].vmaf +
                 t * (probes[hi_idx].vmaf - probes[lo_idx].vmaf);
      rec_kbps = probes[lo_idx].kbps +
                 ((double)(rec_crf - lo_crf) / (double)(hi_crf - lo_crf)) *
                     (probes[hi_idx].kbps - probes[lo_idx].kbps);
    }
  } else {
    /* No bracket: pick the probe closest to target. */
    int best = 0;
    double best_diff = fabs(probes[0].vmaf - target);
    for (int i = 1; i < n_probes; i++) {
      double d = fabs(probes[i].vmaf - target);
      if (d < best_diff) {
        best_diff = d;
        best = i;
      }
    }
    rec_crf = probes[best].crf;
    rec_vmaf = probes[best].vmaf;
    rec_kbps = probes[best].kbps;
  }

  result.recommended_crf = rec_crf;
  result.measured_vmaf_mean = rec_vmaf;
  result.measured_bitrate_kbps = (int)(rec_kbps + 0.5);

  int total_sec = (int)difftime(time(NULL), search_start);
  printf("  %-7s  %7s  %9s  %7s\n",
         "-------", "-------", "---------", "-------");
  printf("\n  Result: CRF %d -> %d kbps (mean VMAF %.2f) in %02d:%02d, "
         "%d probes\n",
         rec_crf, result.measured_bitrate_kbps, rec_vmaf,
         total_sec / 60, total_sec % 60, n_probes);

  return result;
}
