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
  /* A 4th digit after the episode means it's not an episode tag. */
  CHECK(parse_season_episode("Show.S01E1050.1080p.mkv", &s, &e) == -1);
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

static void test_build_output_filename_movie(void) {
  MediaInfo info;
  memset(&info, 0, sizeof(info));
  info.height = 1080;
  char buf[1024];

  /* NULL episode => movie format, unchanged from before this feature. */
  CHECK(build_output_filename(buf, sizeof(buf), "The Matrix", 1999, LANG_TAG_MULTI, &info, NULL,
                              SOURCE_BLURAY, NULL) == 0);
  CHECK_STR(buf, "The.Matrix.1999.MULTi.1080p.SDR.BluRay.HDLight.10bit.AV1.OPUS-TEST.mkv");
}

static void test_build_output_filename_tv(void) {
  MediaInfo info;
  memset(&info, 0, sizeof(info));
  info.height = 1080;
  char buf[1024];

  EpisodeInfo ep = {.season = 1, .episode = 5};
  snprintf(ep.title, sizeof(ep.title), "Sheridan");
  CHECK(build_output_filename(buf, sizeof(buf), "The Bear", 2022, LANG_TAG_MULTI, &info, NULL,
                              SOURCE_WEBDL, &ep) == 0);
  CHECK_STR(buf, "The.Bear.S01E05.Sheridan.MULTi.1080p.SDR.WEB-DL.HDLight.10bit.AV1.OPUS-TEST.mkv");

  /* Empty episode title => segment omitted (TMDB gap is non-fatal). */
  ep.title[0] = '\0';
  CHECK(build_output_filename(buf, sizeof(buf), "The Bear", 2022, LANG_TAG_MULTI, &info, NULL,
                              SOURCE_WEBDL, &ep) == 0);
  CHECK_STR(buf, "The.Bear.S01E05.MULTi.1080p.SDR.WEB-DL.HDLight.10bit.AV1.OPUS-TEST.mkv");

  /* 3-digit episode. */
  memset(&ep, 0, sizeof(ep));
  ep.season = 1;
  ep.episode = 105;
  CHECK(build_output_filename(buf, sizeof(buf), "One Piece", 1999, LANG_TAG_VOST, &info, NULL,
                              SOURCE_WEBRIP, &ep) == 0);
  CHECK_STR(buf, "One.Piece.S01E105.VOST.1080p.SDR.WEBRip.HDLight.10bit.AV1.OPUS-TEST.mkv");
}

int main(void) {
  test_parse_season_episode();
  test_build_output_filename_movie();
  test_build_output_filename_tv();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("All tests passed\n");
  return 0;
}
