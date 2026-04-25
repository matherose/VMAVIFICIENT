/**
 * @file film_grain.h
 * @brief Parse grav1synth "filmgrn1" grain tables into SVT-AV1's AomFilmGrain.
 */

#ifndef VMAV_FILM_GRAIN_H
#define VMAV_FILM_GRAIN_H

#include <EbSvtAv1.h>

/**
 * @brief Parse a filmgrn1 text table into a freshly allocated AomFilmGrain.
 *
 * Only the first `E` (scene) entry is read — matches SVT-AV1's own
 * reference parser at `read_fgs_table()` in its app_config.c. The encoder
 * currently supports a single grain descriptor per stream, so picking one
 * representative scene is the canonical approach.
 *
 * The returned struct has `apply_grain = 1` and `ignore_ref = 1` set (grain
 * synthesis on, no reference-frame reuse).
 *
 * @param path  Filesystem path to a grav1synth-produced filmgrn1 table.
 * @return Newly allocated AomFilmGrain on success; NULL on I/O or parse
 *         failure. Caller frees with free_film_grain().
 */
AomFilmGrain *parse_filmgrn1_table(const char *path);

/** @brief Free a struct returned by parse_filmgrn1_table(). NULL-safe. */
void free_film_grain(AomFilmGrain *fg);

#endif /* VMAV_FILM_GRAIN_H */
