/**
 * @file crf_search.c
 * @brief Orchestrator: cut samples, encode at probe CRFs, score, solve.
 *
 * Strategy:
 *   1. Cut N short sample clips from the source at evenly spaced offsets
 *      using `ffmpeg -ss -t -c copy` (stream copy — fast, bit-exact, snaps
 *      to nearest keyframe).
 *   2. For each probe CRF, encode every sample with encode_video() in CRF
 *      rate-control mode (the new `.crf` field) and score the result
 *      against its source sample via ssimu2_score_files().
 *   3. Aggregate per-CRF p10 scores across samples (mean), then linearly
 *      interpolate to solve for the target p10. Clamp to [20, 40].
 *
 * Dolby Vision RPU is intentionally NOT attached during the search: RPU is
 * metadata that does not affect AV1 coding quality, so calibration is
 * unaffected, and cutting a matching RPU sidecar for each sample would add
 * fragile tooling. The real encode run still uses the RPU.
 */

#include "crf_search.h"

#include "media_ssimu2.h"
#include "video_encode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ====================================================================== */
/*  Helpers                                                               */
/* ====================================================================== */

static int file_exists_nonempty(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && st.st_size > 0;
}

/**
 * @brief Transcode an encoded AV1 file into a lossless FFV1 intermediate
 *        via the system ffmpeg binary.
 *
 * The project's vendored libav is built without libdav1d / libaom, so its
 * native "av1" decoder is hwaccel-only and can't decode AV1 in software.
 * Rather than bloat the vendored ffmpeg build, we use the system ffmpeg
 * (which has libdav1d) to round-trip the AV1 clip through FFV1 — a
 * mathematically lossless intra-only codec that our vendored libav reads
 * natively. This preserves per-pixel values exactly, so SSIMULACRA2 scores
 * are unaffected.
 */
static int transcode_av1_to_ffv1(const char *src_av1, const char *dst_ffv1) {
  if (file_exists_nonempty(dst_ffv1))
    return 0;
  char cmd[4096];
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -y -loglevel error -nostdin -i \"%s\" "
           "-map 0:v:0 -c:v ffv1 -level 3 -pix_fmt yuv420p10le "
           "-an -sn -dn \"%s\"",
           src_av1, dst_ffv1);
  int rc = system(cmd);
  if (rc != 0 || !file_exists_nonempty(dst_ffv1)) {
    fprintf(stderr,
            "  CRF search: ffv1 transcode failed (rc=%d) for %s\n", rc,
            dst_ffv1);
    return -1;
  }
  return 0;
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
           "ffmpeg -y -loglevel error -nostdin -ss %d -i \"%s\" -t %d "
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
      .film_grain = cfg->film_grain,
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

/**
 * @brief Linearly interpolate @p probes (CRF → p10) to solve for @p target.
 *
 * `crfs` is a sorted-ascending array of CRF values; `scores` is the matching
 * p10 for each. As CRF rises, p10 drops, so the relationship is monotonic
 * decreasing. We find the bracketing segment and interpolate.
 *
 * If the target lies outside the probed range we clamp to the closest
 * endpoint (better than extrapolating off a 3-point fit).
 */
static int solve_crf(const int *crfs, const double *scores, int n,
                     double target) {
  if (n <= 0)
    return -1;
  if (n == 1)
    return crfs[0];

  /* Scores are monotonic-decreasing in CRF. Walk to find bracket. */
  for (int i = 0; i < n - 1; i++) {
    double s_hi = scores[i];     /* higher score (lower CRF) */
    double s_lo = scores[i + 1]; /* lower score (higher CRF) */
    if (target <= s_hi && target >= s_lo) {
      double span = s_hi - s_lo;
      if (span < 1e-6)
        return crfs[i];
      double t = (s_hi - target) / span;
      double crf = crfs[i] + t * (crfs[i + 1] - crfs[i]);
      return (int)(crf + 0.5);
    }
  }
  /* Out of range: clamp to the endpoint closest in score. */
  if (target > scores[0])
    return crfs[0];
  return crfs[n - 1];
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

  /* Count probes (0-terminated). */
  int n_probes = 0;
  while (n_probes < 4 && cfg->crf_probes[n_probes] > 0)
    n_probes++;
  if (n_probes == 0) {
    fprintf(stderr, "  CRF search: no probes configured\n");
    result.error = -1;
    return result;
  }

  int n_samples = cfg->sample_count > 0 ? cfg->sample_count : 2;
  int dur = cfg->sample_duration > 0 ? cfg->sample_duration : 10;
  int stride = cfg->frame_stride > 0 ? cfg->frame_stride : 1;

  /* Source duration determines sample placement. Space samples evenly at
   * 25%, 50%, 75% ... of the timeline; skip the first and last 5%. */
  double src_dur = cfg->info->duration;
  if (src_dur < (double)(dur * 2)) {
    fprintf(stderr,
            "  CRF search: source too short (%.1fs) for %d samples x %ds\n",
            src_dur, n_samples, dur);
    result.error = -1;
    return result;
  }

  /* Sample offsets */
  int offsets[8] = {0};
  if (n_samples > 8)
    n_samples = 8;
  double usable_start = src_dur * 0.05;
  double usable_end = src_dur * 0.95 - dur;
  if (usable_end <= usable_start)
    usable_end = usable_start + dur;
  for (int i = 0; i < n_samples; i++) {
    double frac = (n_samples == 1) ? 0.5 : (double)(i + 1) / (n_samples + 1);
    offsets[i] = (int)(usable_start + frac * (usable_end - usable_start));
  }

  /* ---- Cut samples ---- */
  char sample_paths[8][1024];
  char ref_paths[8][1024]; /* scoring reference: cropped FFV1 if crop active */

  printf("\nOptimal bitrate search (target SSIMULACRA2 p10 >= %.2f)\n",
         cfg->target_p10);
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
    snprintf(sample_paths[i], sizeof(sample_paths[i]),
             "%s/crfsearch_sample_%d.mkv", cfg->workdir, i);
    if (cut_sample(cfg->input_path, sample_paths[i], offsets[i], dur) < 0) {
      result.error = -1;
      return result;
    }

    /* Create cropped FFV1 reference for scoring (once per sample). */
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
      /* No crop: use the raw sample directly as reference. */
      snprintf(ref_paths[i], sizeof(ref_paths[i]), "%s", sample_paths[i]);
    }
  }

  printf("\n  %-7s  %8s  %10s  %7s  %s\n",
         "CRF", "p10", "kbps", "time", "");
  printf("  %-7s  %8s  %10s  %7s  %s\n",
         "-------", "--------", "----------", "-------", "----------");
  fflush(stdout);

  /* ---- Probe each CRF ---- */
  double probe_p10[4] = {0};
  double probe_kbps[4] = {0};
  int probe_ok[4] = {0};
  time_t search_start = time(NULL);

  for (int p = 0; p < n_probes; p++) {
    int crf = cfg->crf_probes[p];
    double sum_p10 = 0.0;
    double sum_kbps = 0.0;
    int scored = 0;
    time_t probe_start = time(NULL);

    /* Print in-progress indicator on stderr (overwritten by final result) */
    fprintf(stderr, "\r  CRF %-3d  probing...                                   ",
            crf);
    fflush(stderr);

    for (int i = 0; i < n_samples; i++) {
      char enc_path[1024];
      snprintf(enc_path, sizeof(enc_path), "%s/crfsearch_sample_%d_crf%d.mkv",
               cfg->workdir, i, crf);

      if (encode_sample_at_crf(cfg, sample_paths[i], enc_path, crf) < 0)
        continue;

      /* Measure encoded bitrate — file size / requested duration. */
      struct stat enc_st;
      double this_kbps = 0.0;
      if (stat(enc_path, &enc_st) == 0 && enc_st.st_size > 0 && dur > 0) {
        this_kbps = (double)enc_st.st_size * 8.0 / 1000.0 / (double)dur;
      }

      /* Transcode encoded AV1 -> lossless FFV1 so our libav can decode it. */
      char ffv1_path[1024];
      snprintf(ffv1_path, sizeof(ffv1_path),
               "%s/crfsearch_sample_%d_crf%d.ffv1.mkv", cfg->workdir, i, crf);
      if (transcode_av1_to_ffv1(enc_path, ffv1_path) < 0)
        continue;

      Ssimu2Result sr =
          ssimu2_score_files(ref_paths[i], ffv1_path, stride);
      if (sr.error < 0) {
        fprintf(stderr,
                "\r  CRF %-3d  score failed (err=%d)                       \n",
                crf, sr.error);
        continue;
      }
      sum_p10 += sr.p10;
      sum_kbps += this_kbps;
      scored++;
    }

    time_t probe_end = time(NULL);
    int probe_sec = (int)difftime(probe_end, probe_start);

    if (scored == 0) {
      fprintf(stderr,
              "\r  CRF %-3d  failed                                       \n",
              crf);
      continue;
    }
    probe_p10[p] = sum_p10 / scored;
    probe_kbps[p] = sum_kbps / scored;
    probe_ok[p] = 1;
    result.probes_succeeded++;

    /* ETA: average time per completed probe × remaining probes */
    int probes_done = p + 1;
    int probes_left = n_probes - probes_done;
    int elapsed_total = (int)difftime(probe_end, search_start);
    char eta_str[32] = "";
    if (probes_left > 0 && probes_done > 0) {
      int avg_per_probe = elapsed_total / probes_done;
      int eta_sec = avg_per_probe * probes_left;
      snprintf(eta_str, sizeof(eta_str), "ETA %02d:%02d",
               eta_sec / 60, eta_sec % 60);
    }

    /* Clear the in-progress line and print final result for this probe */
    fprintf(stderr, "\r");
    printf("  CRF %-3d  %7.2f   %8.0f    %02d:%02d   %s\n",
           crf, probe_p10[p], probe_kbps[p],
           probe_sec / 60, probe_sec % 60, eta_str);
    fflush(stdout);
  }

  if (result.probes_succeeded < 1) {
    result.error = -1;
    return result;
  }

  /* Compact ok probes into a dense array for the solver. */
  int crfs_ok[4];
  double scores_ok[4];
  double kbps_ok[4];
  int n_ok = 0;
  for (int p = 0; p < n_probes; p++) {
    if (probe_ok[p]) {
      crfs_ok[n_ok] = cfg->crf_probes[p];
      scores_ok[n_ok] = probe_p10[p];
      kbps_ok[n_ok] = probe_kbps[p];
      n_ok++;
    }
  }

  int rec = solve_crf(crfs_ok, scores_ok, n_ok, cfg->target_p10);
  if (rec < 20)
    rec = 20;
  if (rec > 55)
    rec = 55;

  result.recommended_crf = rec;

  /* Predicted p10 and kbps at `rec`: linear-interp. Out-of-range clamps to
   * the nearest endpoint. */
  double pred = scores_ok[0];
  double pred_kbps = kbps_ok[0];
  if (rec <= crfs_ok[0]) {
    pred = scores_ok[0];
    pred_kbps = kbps_ok[0];
  } else if (rec >= crfs_ok[n_ok - 1]) {
    pred = scores_ok[n_ok - 1];
    pred_kbps = kbps_ok[n_ok - 1];
  } else {
    for (int i = 0; i < n_ok - 1; i++) {
      if (rec >= crfs_ok[i] && rec <= crfs_ok[i + 1]) {
        double t =
            (double)(rec - crfs_ok[i]) / (double)(crfs_ok[i + 1] - crfs_ok[i]);
        pred = scores_ok[i] + t * (scores_ok[i + 1] - scores_ok[i]);
        pred_kbps = kbps_ok[i] + t * (kbps_ok[i + 1] - kbps_ok[i]);
        break;
      }
    }
  }
  result.predicted_p10 = pred;
  result.measured_bitrate_kbps = (int)(pred_kbps + 0.5);

  int total_sec = (int)difftime(time(NULL), search_start);
  printf("  %-7s  %8s  %10s  %7s\n",
         "-------", "--------", "----------", "-------");
  printf("\n  Result: CRF %d -> %d kbps (predicted p10 = %.2f) in %02d:%02d\n",
         rec, result.measured_bitrate_kbps, pred,
         total_sec / 60, total_sec % 60);

  return result;
}
