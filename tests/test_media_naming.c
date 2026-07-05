/**
 * @file test_media_naming.c
 * @brief Unit tests for media_naming (season/episode parsing, filenames).
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "media_naming.h"

/* Stub: media_naming.c calls config_get() for the release group; the real
   implementation reads config.ini, which tests must not depend on. */
const VmavConfig *config_get(void) {
  static VmavConfig cfg;
  if (!cfg.release_group[0])
    snprintf(cfg.release_group, sizeof(cfg.release_group), "TEST");
  return &cfg;
}

static int failures = 0;

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
      failures++;                                                                                  \
    }                                                                                              \
  } while (0)

#define CHECK_STR(actual, expected)                                                                \
  do {                                                                                             \
    if (strcmp((actual), (expected)) != 0) {                                                       \
      fprintf(stderr, "FAIL %s:%d:\n  got:  \"%s\"\n  want: \"%s\"\n", __FILE__, __LINE__,         \
              (actual), (expected));                                                               \
      failures++;                                                                                  \
    }                                                                                              \
  } while (0)

static void test_parse_season_episode(void) {
  int s = 0, e = 0;

  /* Standard SxxEyy in scene names. */
  CHECK(parse_season_episode("Show.S01E05.MULTi.1080p.WEB-DL.mkv", &s, &e) == 0 && s == 1 &&
        e == 5);
  s = e = 0;
  CHECK(parse_season_episode("show.s02e17.mkv", &s, &e) == 0 && s == 2 && e == 17);
  s = e = 0;
  CHECK(parse_season_episode("Show.S01.E05.mkv", &s, &e) == 0 && s == 1 && e == 5);
  s = e = 0;
  CHECK(parse_season_episode("Show 1x05.mkv", &s, &e) == 0 && s == 1 && e == 5);
  s = e = 0;
  CHECK(parse_season_episode("Show.1x05.mkv", &s, &e) == 0 && s == 1 && e == 5);
  s = e = 0;
  /* 3-digit episodes (long-running anime). */
  CHECK(parse_season_episode("Show.S01E105.mkv", &s, &e) == 0 && s == 1 && e == 105);
  s = e = 0;
  /* Double episode: first number wins (documented v1 limitation). */
  CHECK(parse_season_episode("Show.S01E05E06.mkv", &s, &e) == 0 && s == 1 && e == 5);

  /* Negatives: resolutions, years, plain movie names must not parse. */
  CHECK(parse_season_episode("Movie.2023.1920x1080.BluRay.mkv", &s, &e) != 0);
  CHECK(parse_season_episode("Movie.720x480.DVDRip.mkv", &s, &e) != 0);
  CHECK(parse_season_episode("Some.Movie.2019.1080p.BluRay.mkv", &s, &e) != 0);
  CHECK(parse_season_episode("Neon Genesis Evangelion - Episode 01.mkv", &s, &e) != 0);
  /* 'S<digit>E<digit>' embedded in a word is not a season tag. */
  CHECK(parse_season_episode("Game.PS4E10.Gameplay.mkv", &s, &e) != 0);
  CHECK(parse_season_episode(NULL, &s, &e) != 0);
}

int main(void) {
  test_parse_season_episode();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("All tests passed\n");
  return 0;
}
