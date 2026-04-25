/**
 * @file film_grain.c
 * @brief Parse grav1synth "filmgrn1" grain tables into AomFilmGrain.
 *
 * Ported from SVT-AV1's reference parser (`read_fgs_table()` in
 * Source/App/app_config.c). grav1synth's "diff" subcommand produces the
 * exact same format, so we reuse the reference parsing logic verbatim —
 * reading only the first `E` entry, which is what the encoder consumes
 * via `EbSvtAv1EncConfiguration.fgs_table` anyway.
 */

#include "film_grain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AomFilmGrain *parse_filmgrn1_table(const char *path) {
  if (!path || !path[0])
    return NULL;

  FILE *f = fopen(path, "r");
  if (!f)
    return NULL;

  char magic[9] = {0};
  if (fread(magic, 1, 9, f) != 9 || strncmp(magic, "filmgrn1", 8) != 0) {
    fclose(f);
    return NULL;
  }

  AomFilmGrain *fg = calloc(1, sizeof(*fg));
  if (!fg) {
    fclose(f);
    return NULL;
  }

  int num_read = fscanf(f, "E %*d %*d %d %hu %d\n", &fg->apply_grain,
                        &fg->random_seed, &fg->update_parameters);
  if (num_read != 3)
    goto fail;

  if (fg->update_parameters) {
    num_read = fscanf(
        f, "p %d %d %d %d %d %d %d %d %d %d %d %d\n", &fg->ar_coeff_lag,
        &fg->ar_coeff_shift, &fg->grain_scale_shift, &fg->scaling_shift,
        &fg->chroma_scaling_from_luma, &fg->overlap_flag, &fg->cb_mult,
        &fg->cb_luma_mult, &fg->cb_offset, &fg->cr_mult, &fg->cr_luma_mult,
        &fg->cr_offset);
    if (num_read != 12)
      goto fail;

    if (!fscanf(f, "\tsY %d ", &fg->num_y_points))
      goto fail;
    if (fg->num_y_points < 0 || fg->num_y_points > 14)
      goto fail;
    for (int i = 0; i < fg->num_y_points; ++i) {
      if (fscanf(f, "%d %d", &fg->scaling_points_y[i][0],
                 &fg->scaling_points_y[i][1]) != 2)
        goto fail;
    }

    if (!fscanf(f, "\n\tsCb %d", &fg->num_cb_points))
      goto fail;
    if (fg->num_cb_points < 0 || fg->num_cb_points > 10)
      goto fail;
    for (int i = 0; i < fg->num_cb_points; ++i) {
      if (fscanf(f, "%d %d", &fg->scaling_points_cb[i][0],
                 &fg->scaling_points_cb[i][1]) != 2)
        goto fail;
    }

    if (!fscanf(f, "\n\tsCr %d", &fg->num_cr_points))
      goto fail;
    if (fg->num_cr_points < 0 || fg->num_cr_points > 10)
      goto fail;
    for (int i = 0; i < fg->num_cr_points; ++i) {
      if (fscanf(f, "%d %d", &fg->scaling_points_cr[i][0],
                 &fg->scaling_points_cr[i][1]) != 2)
        goto fail;
    }

    /* fscanf of a string literal returns 0 when matched (no conversion specs);
     * treat a nonzero return as header-mismatch and fail, matching the
     * reference parser's behavior. */
    if (fscanf(f, "\n\tcY"))
      goto fail;
    const int n = 2 * fg->ar_coeff_lag * (fg->ar_coeff_lag + 1);
    if (n < 0 || n > 24)
      goto fail;
    for (int i = 0; i < n; ++i) {
      if (fscanf(f, "%d", &fg->ar_coeffs_y[i]) != 1)
        goto fail;
    }

    if (fscanf(f, "\n\tcCb"))
      goto fail;
    for (int i = 0; i <= n; ++i) {
      if (fscanf(f, "%d", &fg->ar_coeffs_cb[i]) != 1)
        goto fail;
    }

    if (fscanf(f, "\n\tcCr"))
      goto fail;
    for (int i = 0; i <= n; ++i) {
      if (fscanf(f, "%d", &fg->ar_coeffs_cr[i]) != 1)
        goto fail;
    }
  }

  fclose(f);

  /* Force grain-synthesis on and disable ref-frame reuse — matches the
   * reference parser's post-processing so the encoder actually emits grain. */
  fg->apply_grain = 1;
  fg->ignore_ref = 1;
  return fg;

fail:
  free(fg);
  fclose(f);
  return NULL;
}

void free_film_grain(AomFilmGrain *fg) { free(fg); }
