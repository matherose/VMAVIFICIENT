/**
 * @file crf_search.c
 * @brief CRF search via ab-av1 crf-search subprocess.
 *
 * Runs `ab-av1 crf-search` with the full set of SVT-AV1 encoder args derived
 * from the active preset so the probe encodes use the exact same quality
 * configuration as the real encode.  stdout is captured to parse the final
 * "crf <N>" result; stderr is left inherited so ab-av1's progress reaches
 * the terminal.
 */

#include "crf_search.h"

#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Each --svt key=value pair costs 2 argv slots. Allow up to 64 --svt args
 * on top of the 8 base args (ab-av1 crf-search -i <in> --preset N --min-vmaf N).
 * One extra slot for the terminating NULL. */
#define MAX_SVT_ARGS 64
#define SVT_VAL_LEN  64
#define MAX_ARGV     (8 + MAX_SVT_ARGS * 2 + 1)

CrfSearchResult run_crf_search(const char *input_path, int vmaf_target,
                               const EncodePreset *p, int film_grain) {
  CrfSearchResult result = {.crf = -1, .vmaf_target = vmaf_target,
                            .ab_av1_missing = 0};

  /* All --svt value strings live here; pointers in argv[] remain valid
   * through the posix_spawnp() call. */
  char svt_vals[MAX_SVT_ARGS][SVT_VAL_LEN];
  char *argv[MAX_ARGV];
  int svt_n = 0, argc = 0;

  char preset_str[8], vmaf_str[8];
  snprintf(preset_str, sizeof(preset_str), "%d", p->preset);
  snprintf(vmaf_str,   sizeof(vmaf_str),   "%d", vmaf_target);

  argv[argc++] = "ab-av1";
  argv[argc++] = "crf-search";
  argv[argc++] = "-i";
  argv[argc++] = (char *)input_path;
  argv[argc++] = "--preset";
  argv[argc++] = preset_str;
  argv[argc++] = "--min-vmaf";
  argv[argc++] = vmaf_str;

/* Append one --svt key=value arg.  Silently no-ops when buffers are full. */
#define ADD_SVT(fmt, ...)                                            \
  do {                                                               \
    if (svt_n < MAX_SVT_ARGS && argc + 2 < MAX_ARGV) {              \
      snprintf(svt_vals[svt_n], SVT_VAL_LEN, fmt, ##__VA_ARGS__);   \
      argv[argc++] = "--svt";                                        \
      argv[argc++] = svt_vals[svt_n++];                             \
    }                                                                \
  } while (0)

  /* ---- Perceptual / quality params — always set ---- */
  ADD_SVT("tune=%d",                         p->tune);
  ADD_SVT("enable-tf=%d",                    p->enable_tf);
  ADD_SVT("tf-strength=%d",                  p->tf_strength);
  ADD_SVT("kf-tf-strength=%d",               p->kf_tf_strength);
  ADD_SVT("sharpness=%d",                    p->sharpness);
  ADD_SVT("ac-bias=%.2f",                    p->ac_bias);
  ADD_SVT("enable-variance-boost=1");
  ADD_SVT("variance-boost-strength=%d",      p->variance_boost);
  ADD_SVT("variance-octile=%d",              p->variance_octile);
  ADD_SVT("variance-boost-curve=%d",         p->variance_curve);
  ADD_SVT("cdef-scaling=%d",                 p->cdef_scaling);
  ADD_SVT("hbd-mds=%d",                      p->hbd_mds);
  ADD_SVT("noise-adaptive-filtering=%d",     p->noise_adaptive_filtering);
  ADD_SVT("complex-hvs=%d",                  p->complex_hvs);
  ADD_SVT("qp-scale-compress-strength=%.2f", p->qp_scale_compress_strength);
  ADD_SVT("sharp-tx=%d",                     p->sharp_tx);
  ADD_SVT("tx-bias=%d",                      p->tx_bias);
  ADD_SVT("luminance-qp-bias=%d",            p->luminance_bias);
  ADD_SVT("alt-lambda-factors=%d",           p->alt_lambda_factors);
  ADD_SVT("adaptive-film-grain=%d",          p->adaptive_film_grain);
  ADD_SVT("max-tx-size=%d",                  p->max_tx_size);

  /* ---- Conditional params: -1 means "use encoder default", skip ---- */
  if (p->noise_norm_strength >= 0)
    ADD_SVT("noise-norm-strength=%d",            p->noise_norm_strength);
  if (p->chroma_qm_min >= 0)
    ADD_SVT("chroma-qm-min=%d",                  p->chroma_qm_min);
  if (p->chroma_qm_max >= 0)
    ADD_SVT("chroma-qm-max=%d",                  p->chroma_qm_max);
  if (p->qm_min >= 0)
    ADD_SVT("qm-min=%d",                         p->qm_min);
  if (p->qm_max >= 0)
    ADD_SVT("qm-max=%d",                         p->qm_max);
  if (p->key_frame_qindex_offset != 0)
    ADD_SVT("key-frame-qindex-offset=%d",        p->key_frame_qindex_offset);
  if (p->key_frame_chroma_qindex_offset != 0)
    ADD_SVT("key-frame-chroma-qindex-offset=%d", p->key_frame_chroma_qindex_offset);
  if (p->enable_dg >= 0)
    ADD_SVT("enable-dg=%d",                      p->enable_dg);
  if (p->irefresh_type >= 0)
    ADD_SVT("irefresh-type=%d",                  p->irefresh_type);
  if (p->aq_mode >= 0)
    ADD_SVT("aq-mode=%d",                        p->aq_mode);
  if (p->recode_loop >= 0)
    ADD_SVT("recode-loop=%d",                    p->recode_loop);
  if (p->look_ahead_distance >= 0)
    ADD_SVT("lookahead=%d",                      p->look_ahead_distance);
  if (p->fast_decode >= 0)
    ADD_SVT("fast-decode=%d",                    p->fast_decode);
  if (p->startup_mg_size > 0)
    ADD_SVT("startup-mg-size=%d",                p->startup_mg_size);
  if (p->startup_qp_offset != 0)
    ADD_SVT("startup-qp-offset=%d",              p->startup_qp_offset);
  if (p->enable_mfmv >= 0)
    ADD_SVT("enable-mfmv=%d",                    p->enable_mfmv);
  if (p->enable_overlays >= 0)
    ADD_SVT("enable-overlays=%d",                p->enable_overlays);
  if (p->enable_restoration >= 0)
    ADD_SVT("enable-restoration=%d",             p->enable_restoration);
  if (p->min_qp >= 0)
    ADD_SVT("min-qp=%d",                         p->min_qp);
  if (p->max_qp >= 0)
    ADD_SVT("max-qp=%d",                         p->max_qp);
  /* scd is a dedicated ab-av1 flag (--scd true|false); passing it via
   * --svt is rejected. For short sample encodes it doesn't affect CRF
   * calibration, so we leave it at ab-av1's default. */

  /* ---- Grain / noise mechanism ---- */
  if (p->use_noise) {
    /* Digital / animation: synthetic overlay via --noise. */
    if (film_grain > 0) {
      ADD_SVT("noise=%d", film_grain);
      if (p->noise_chroma_strength >= 0)
        ADD_SVT("noise-chroma=%d",          p->noise_chroma_strength);
      if (p->noise_chroma_from_luma)
        ADD_SVT("noise-chroma-from-luma=1");
      if (p->noise_size >= 0)
        ADD_SVT("noise-size=%d",            p->noise_size);
    }
  } else {
    /* Analog film: analysed grain + denoise via --film-grain. */
    if (film_grain > 0) {
      ADD_SVT("film-grain=%d",         film_grain);
      ADD_SVT("film-grain-denoise=1");
    }
  }

#undef ADD_SVT

  argv[argc] = NULL;

  /* ---- Subprocess: stdout → pipe, stderr → terminal ---- */
  int pipefd[2];
  if (pipe(pipefd) != 0)
    return result;

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipefd[1]);

  pid_t pid;
  int ret = posix_spawnp(&pid, "ab-av1", &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);

  if (ret != 0) {
    close(pipefd[0]);
    result.ab_av1_missing = 1;
    return result;
  }

  /* ---- Parse stdout: keep last "crf <N>" match ---- */
  FILE *f = fdopen(pipefd[0], "r");
  if (!f) {
    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    return result;
  }

  char line[512];
  int found_crf = -1;
  while (fgets(line, sizeof(line), f)) {
    int c;
    if (sscanf(line, "crf %d", &c) == 1 && c >= 1 && c <= 63)
      found_crf = c;
  }
  fclose(f);

  int status = 0;
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0)
    return result;

  result.crf = found_crf;
  return result;
}
