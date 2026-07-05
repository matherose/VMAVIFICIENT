# TV Show Support (v1: single episode) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Encode a single TV episode with TMDB TV metadata and scene-convention naming (`Show.S01E05.Episode.Title.<LANG>...`), per the approved spec `docs/superpowers/specs/2026-07-05-tv-show-support-design.md`.

**Architecture:** Unified naming — `build_output_filename()` gains a `const EpisodeInfo *ep` parameter (`NULL` = movie, byte-for-byte unchanged). The TMDB module gains `tmdb_fetch_tv()` / `tmdb_fetch_episode()` sharing a new curl-GET-JSON helper. `main.c` resolves season/episode (flags → filename parse → prompt) and feeds a common metadata path.

**Tech Stack:** C11, CMake + Ninja (`cmake --build build`), libcurl, cJSON. No test infrastructure exists yet — Task 1 bootstraps a minimal one.

**Conventions (repo-specific, read first):**
- Commits end with `Assisted-by: Claude Fable 5 <noreply@anthropic.com>` — NOT `Co-Authored-By`.
- `git add` with **explicit paths only** — never `git add -A` (huge untracked media files in the worktree, e.g. `Neon Genesis Evangelion - Episode 01.mkv`, must never be staged).
- Do NOT run clang-format manually (local v22 ≠ pinned v18); the pre-commit hook handles formatting. Match surrounding style by hand.
- Never use scanf-family for new input parsing (cert-err33/34 lints gate CI); use `fgets` + the existing `parse_int_or_zero()` helper, like `ask_source()` does.
- The build dir `build/` is already configured (Ninja). `cmake --build build --target vmav_tests` builds only the test binary; `cmake --build build` builds everything.

---

## Task 1: Test harness + `parse_season_episode`

**Files:**
- Create: `tests/test_media_naming.c`
- Modify: `CMakeLists.txt` (append at end of file)
- Modify: `include/vmavificient/media_naming.h` (declaration)
- Modify: `src/media_naming/media_naming.c` (implementation)

- [ ] **Step 1: Create the test file with failing tests**

Create `tests/test_media_naming.c`:

```c
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
```

- [ ] **Step 2: Add the test target to CMake**

Append at the **very end** of `CMakeLists.txt`:

```cmake
# ---------------------------------------------------------------------------
# Unit tests (pure-logic modules only; no external deps linked).
#   Build:  cmake --build build --target vmav_tests
#   Run:    ./build/vmav_tests
# ---------------------------------------------------------------------------
add_executable(vmav_tests
    tests/test_media_naming.c
    src/media_naming/media_naming.c
)
target_include_directories(vmav_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/include/vmavificient"
)
```

(`config_get()` is stubbed inside the test file, so `src/config/config.c` must NOT be listed — it would be a duplicate symbol.)

- [ ] **Step 3: Verify the test fails to build (function doesn't exist yet)**

Run: `cmake --build build --target vmav_tests`
Expected: **compile error** — `parse_season_episode` undeclared.

- [ ] **Step 4: Declare `parse_season_episode` in the header**

In `include/vmavificient/media_naming.h`, after the `language_tag_to_string` declaration and before the `build_output_filename` doc comment, insert:

```c
/**
 * @brief Parse season/episode numbers from a filename.
 *
 * Recognized patterns, in priority order:
 *   1. SxxEyy — any case, optional single separator between the groups
 *      (S01E05, s02e17, S01.E05). Episode may be up to 3 digits.
 *   2. NxNN (1x05) — guarded: both values capped at 2 digits and the
 *      match must be separator-bounded, so 1920x1080 never parses.
 *
 * Double episodes (S01E05E06) yield the first episode number.
 *
 * @param filename  Filename to scan (basename preferred).
 * @param season    Out: season number.
 * @param episode   Out: episode number.
 * @return 0 on success, -1 if no pattern matched.
 */
int parse_season_episode(const char *filename, int *season, int *episode);
```

- [ ] **Step 5: Implement in `src/media_naming/media_naming.c`**

Insert before the `/* ── Filename builder ─...` section (which precedes `sanitize_title` at ~line 293):

```c
/* ── Season/episode parsing ────────────────────────────────────── */

static int is_name_sep(char c) { return c == '.' || c == '_' || c == ' ' || c == '-'; }

int parse_season_episode(const char *filename, int *season, int *episode) {
  if (!filename || !season || !episode)
    return -1;

  /* Pass 1: SxxEyy with an optional single separator between the groups.
     The 'S' must sit on a word boundary so e.g. "PS4E10" is not a match. */
  for (const char *p = filename; *p; p++) {
    if ((*p != 'S' && *p != 's') || !isdigit((unsigned char)p[1]))
      continue;
    if (p != filename && isalnum((unsigned char)p[-1]))
      continue;

    const char *q = p + 1;
    int s = 0, ndig = 0;
    while (isdigit((unsigned char)*q) && ndig < 2) {
      s = s * 10 + (*q - '0');
      q++;
      ndig++;
    }
    if (isdigit((unsigned char)*q))
      continue; /* 3+ digit "season": not a season tag */
    if (is_name_sep(*q))
      q++;
    if ((*q == 'E' || *q == 'e') && isdigit((unsigned char)q[1])) {
      q++;
      int e = 0;
      ndig = 0;
      while (isdigit((unsigned char)*q) && ndig < 3) {
        e = e * 10 + (*q - '0');
        q++;
        ndig++;
      }
      if (s > 0 && e > 0) {
        *season = s;
        *episode = e;
        return 0;
      }
    }
  }

  /* Pass 2: NxNN (1x05), guarded against resolutions: both values are
     capped at 2 digits and the match must be separator-bounded, so
     1920x1080 / 720x480 never parse. */
  for (const char *p = filename; *p; p++) {
    if (!isdigit((unsigned char)*p))
      continue;
    if (p != filename && !is_name_sep(p[-1]))
      continue;

    const char *q = p;
    int s = 0, ndig = 0;
    while (isdigit((unsigned char)*q) && ndig < 2) {
      s = s * 10 + (*q - '0');
      q++;
      ndig++;
    }
    if (isdigit((unsigned char)*q))
      continue; /* 3+ digits: resolution width or year */
    if (*q != 'x' && *q != 'X')
      continue;
    q++;
    int e = 0;
    ndig = 0;
    while (isdigit((unsigned char)*q) && ndig < 2) {
      e = e * 10 + (*q - '0');
      q++;
      ndig++;
    }
    if (ndig == 0 || isdigit((unsigned char)*q))
      continue; /* no episode digits, or 3+ (e.g. x1080) */
    if (s > 0 && e > 0) {
      *season = s;
      *episode = e;
      return 0;
    }
  }

  return -1;
}
```

(`<ctype.h>` is already included in this file.)

- [ ] **Step 6: Run tests, verify pass**

Run: `cmake --build build --target vmav_tests && ./build/vmav_tests`
Expected: `All tests passed`, exit 0.

- [ ] **Step 7: Commit**

```bash
git add tests/test_media_naming.c CMakeLists.txt include/vmavificient/media_naming.h src/media_naming/media_naming.c
git commit -m "feat(media_naming): parse season/episode from filenames + test harness

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2: `EpisodeInfo` + TV output filename

**Files:**
- Modify: `include/vmavificient/media_naming.h`
- Modify: `src/media_naming/media_naming.c` (`build_output_filename`, ~line 310)
- Modify: `src/main/main.c` (two call sites: ~line 1339 and ~line 2054 — add trailing `NULL`)
- Test: `tests/test_media_naming.c`

- [ ] **Step 1: Add failing tests**

In `tests/test_media_naming.c`, add before `main()`:

```c
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
  ep.episode = 105;
  CHECK(build_output_filename(buf, sizeof(buf), "One Piece", 1999, LANG_TAG_VOST, &info, NULL,
                              SOURCE_WEBRIP, &ep) == 0);
  CHECK_STR(buf, "One.Piece.S01E105.VOST.1080p.SDR.WEBRip.HDLight.10bit.AV1.OPUS-TEST.mkv");
}
```

And call both from `main()` after `test_parse_season_episode();`:

```c
  test_build_output_filename_movie();
  test_build_output_filename_tv();
```

(If `LANG_TAG_MULTI` renders differently than `MULTi` or `LANG_TAG_VOST` than `VOST`, check `language_tag_to_string()` in `src/media_naming/media_naming.c` and fix the *expected strings* to match its output — the enum-to-string mapping is pre-existing behavior, not under test.)

- [ ] **Step 2: Verify failure**

Run: `cmake --build build --target vmav_tests`
Expected: **compile error** — `build_output_filename` takes 8 arguments, 9 given (and `EpisodeInfo` undeclared).

- [ ] **Step 3: Extend the header**

In `include/vmavificient/media_naming.h`:

Insert after the `FrenchVariant` enum:

```c
/**
 * @brief TV episode identity for output naming.
 */
typedef struct {
  int season;      /**< Season number (1-based). */
  int episode;     /**< Episode number (1-based). */
  char title[256]; /**< Episode title from TMDB; empty string = omit. */
} EpisodeInfo;
```

Replace the `build_output_filename` doc comment and declaration with:

```c
/**
 * @brief Build the standardized output filename.
 *
 * Movie (ep == NULL):
 * TITLE.YEAR.LANGUAGES.RESOLUTION.FEATURE.SOURCE.QUALITY.10bit.AV1.OPUS-matherose.mkv
 *
 * TV (ep != NULL) — year omitted per scene convention:
 * TITLE.SxxEyy[.EPISODE.TITLE].LANGUAGES.RESOLUTION.FEATURE.SOURCE.QUALITY.10bit.AV1.OPUS-matherose.mkv
 *
 * @param buf       Output buffer.
 * @param bufsize   Size of output buffer.
 * @param title     Original title (movie) or show name (TV), from TMDB.
 * @param year      Release year (unused when ep != NULL).
 * @param lang_tag  Language tag.
 * @param info      Media info (for resolution).
 * @param hdr       HDR info (for features).
 * @param source    Source type.
 * @param ep        Episode identity for TV, or NULL for movies.
 * @return 0 on success, -1 on error.
 */
int build_output_filename(char *buf, size_t bufsize, const char *title, int year,
                          LanguageTag lang_tag, const MediaInfo *info, const HdrInfo *hdr,
                          SourceType source, const EpisodeInfo *ep);
```

- [ ] **Step 4: Extend the implementation**

In `src/media_naming/media_naming.c`, replace the whole `build_output_filename` function with:

```c
int build_output_filename(char *buf, size_t bufsize, const char *title, int year,
                          LanguageTag lang_tag, const MediaInfo *info, const HdrInfo *hdr,
                          SourceType source, const EpisodeInfo *ep) {
  char safe_title[512];
  sanitize_title(safe_title, sizeof(safe_title), title);

  const char *resolution = info->height >= 2160 ? "2160p" : "1080p";
  const char *quality = info->height >= 2160 ? "4KLight" : "HDLight";

  /* Best HDR feature wins: DV > HDR10+ > HDR10 > SDR. */
  char feature[64] = "SDR";
  if (hdr && hdr->error == 0) {
    if (hdr->has_dolby_vision)
      snprintf(feature, sizeof(feature), "DV");
    else if (hdr->has_hdr10plus)
      snprintf(feature, sizeof(feature), "HDR10Plus");
    else if (hdr->has_hdr10)
      snprintf(feature, sizeof(feature), "HDR10");
  }

  /* Head: movie = TITLE.YEAR; TV = TITLE.SxxEyy[.EPISODE.TITLE]
     (year omitted per scene convention). */
  char head[1024];
  if (ep) {
    char safe_ep_title[512] = "";
    if (ep->title[0])
      sanitize_title(safe_ep_title, sizeof(safe_ep_title), ep->title);
    if (safe_ep_title[0])
      snprintf(head, sizeof(head), "%s.S%02dE%02d.%s", safe_title, ep->season, ep->episode,
               safe_ep_title);
    else
      snprintf(head, sizeof(head), "%s.S%02dE%02d", safe_title, ep->season, ep->episode);
  } else {
    snprintf(head, sizeof(head), "%s.%d", safe_title, year);
  }

  /* Assemble:
   * HEAD.LANG.RES.FEATURE.SOURCE.QUALITY.10bit.AV1.OPUS-<group>.mkv */
  const VmavConfig *cfg = config_get();
  snprintf(buf, bufsize, "%s.%s.%s.%s.%s.%s.10bit.AV1.OPUS-%s.mkv", head,
           language_tag_to_string(lang_tag), resolution, feature, source_to_string(source), quality,
           cfg->release_group);

  return 0;
}
```

- [ ] **Step 5: Update the two movie call sites in `src/main/main.c`**

At ~line 1339 (`/* ---- Naming setup ---- */` section):

```c
      build_output_filename(output_name, sizeof(output_name), tmdb.original_title,
                            tmdb.release_year, lang_tag, &info, &hdr, source, NULL);
```

At ~line 2054 (companion-HD naming):

```c
          build_output_filename(hd_output_name, sizeof(hd_output_name), saved_tmdb_title,
                                saved_tmdb_year, resolved_lang_tag, &hd_info, &hd_hdr, source,
                                NULL);
```

- [ ] **Step 6: Run tests and full build, verify pass**

Run: `cmake --build build --target vmav_tests && ./build/vmav_tests`
Expected: `All tests passed`.
Run: `cmake --build build`
Expected: full binary builds with no warnings about the changed signature.

- [ ] **Step 7: Commit**

```bash
git add include/vmavificient/media_naming.h src/media_naming/media_naming.c src/main/main.c tests/test_media_naming.c
git commit -m "feat(media_naming): TV episode output naming via EpisodeInfo

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 3: TMDB TV fetchers

**Files:**
- Modify: `include/vmavificient/tmdb.h`
- Modify: `src/tmdb/tmdb.c`

No unit tests (network-bound); verified end-to-end in Task 6. The refactor must keep `tmdb_fetch_movie()` behavior identical (same error messages, same required-field check).

- [ ] **Step 1: Extend the header**

In `include/vmavificient/tmdb.h`, after the `TmdbMovieInfo` struct + `tmdb_fetch_movie` declaration, add:

```c
/**
 * @brief TV show metadata retrieved from TMDB.
 */
typedef struct {
  int error;                 /**< 0 on success, -1 on failure. */
  char original_name[512];   /**< UTF-8 original show name. */
  int first_air_year;        /**< Year extracted from first_air_date (0 if absent). */
  char original_language[8]; /**< ISO 639-1 code (e.g. "en", "ja"). */
} TmdbTvInfo;

/**
 * @brief Single-episode metadata retrieved from TMDB.
 */
typedef struct {
  int error;      /**< 0 on success, -1 on failure. */
  char name[512]; /**< UTF-8 episode title (localized, default English). */
} TmdbEpisodeInfo;

/**
 * @brief Fetch TV show metadata from TMDB by series ID.
 * @param tmdb_id  The TMDB TV series ID (themoviedb.org/tv/<id>).
 */
TmdbTvInfo tmdb_fetch_tv(int tmdb_id);

/**
 * @brief Fetch one episode's metadata. Failure is expected to be treated
 *        as non-fatal by callers (missing title just omits the segment).
 */
TmdbEpisodeInfo tmdb_fetch_episode(int tmdb_id, int season, int episode);
```

- [ ] **Step 2: Refactor `src/tmdb/tmdb.c` around a shared GET helper**

Keep the file header comment, includes, `ResponseBuf`, and `write_callback` unchanged. Replace everything from `tmdb_fetch_movie` down with:

```c
/**
 * @brief GET a TMDB v3 endpoint and parse the JSON body.
 *
 * @param path  Endpoint path under /3/, without leading slash and without
 *              the api_key parameter (appended here), e.g. "movie/603".
 * @return Parsed document (caller frees with cJSON_Delete) or NULL.
 */
static cJSON *tmdb_get_json(const char *path) {
  const VmavConfig *cfg = config_get();
  if (!cfg->tmdb_api_key[0]) {
    fprintf(stderr, "Error: TMDB API key not found.\n"
                    "Set tmdb_api_key in config.ini (cwd or "
                    "$HOME/.config/vmavificient/config.ini).\n");
    return NULL;
  }

  char url[512];
  snprintf(url, sizeof(url), "https://api.themoviedb.org/3/%s?api_key=%s", path,
           cfg->tmdb_api_key);

  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Error: failed to initialize libcurl.\n");
    return NULL;
  }

  ResponseBuf buf = {.data = NULL, .size = 0};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "Error: TMDB request failed: %s\n", curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    free(buf.data);
    return NULL;
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  if (http_code != 200) {
    fprintf(stderr, "Error: TMDB returned HTTP %ld for %s.\n", http_code, path);
    free(buf.data);
    return NULL;
  }

  cJSON *json = cJSON_Parse(buf.data);
  free(buf.data);
  if (!json)
    fprintf(stderr, "Error: failed to parse TMDB response.\n");
  return json;
}

/** @brief Copy a string field out of a JSON object, if present. */
static void copy_json_string(const cJSON *json, const char *key, char *out, size_t outsize) {
  const cJSON *item = cJSON_GetObjectItem(json, key);
  if (cJSON_IsString(item))
    snprintf(out, outsize, "%s", item->valuestring);
}

/** @brief Extract the year from a "YYYY-MM-DD" date field, or 0. */
static int json_date_year(const cJSON *json, const char *key) {
  const cJSON *item = cJSON_GetObjectItem(json, key);
  int year = 0;
  if (cJSON_IsString(item))
    sscanf(item->valuestring, "%d-", &year);
  return year;
}

TmdbMovieInfo tmdb_fetch_movie(int tmdb_id) {
  TmdbMovieInfo info = {.error = -1, .release_year = 0};
  info.original_title[0] = '\0';
  info.original_language[0] = '\0';

  char path[64];
  snprintf(path, sizeof(path), "movie/%d", tmdb_id);
  cJSON *json = tmdb_get_json(path);
  if (!json)
    return info;

  copy_json_string(json, "original_title", info.original_title, sizeof(info.original_title));
  info.release_year = json_date_year(json, "release_date");
  copy_json_string(json, "original_language", info.original_language,
                   sizeof(info.original_language));
  cJSON_Delete(json);

  if (info.original_title[0] && info.release_year > 0 && info.original_language[0])
    info.error = 0;
  else
    fprintf(stderr, "Error: TMDB response missing required fields.\n");

  return info;
}

TmdbTvInfo tmdb_fetch_tv(int tmdb_id) {
  TmdbTvInfo info = {.error = -1, .first_air_year = 0};
  info.original_name[0] = '\0';
  info.original_language[0] = '\0';

  char path[64];
  snprintf(path, sizeof(path), "tv/%d", tmdb_id);
  cJSON *json = tmdb_get_json(path);
  if (!json)
    return info;

  copy_json_string(json, "original_name", info.original_name, sizeof(info.original_name));
  info.first_air_year = json_date_year(json, "first_air_date");
  copy_json_string(json, "original_language", info.original_language,
                   sizeof(info.original_language));
  cJSON_Delete(json);

  /* first_air_year is informational for TV (not in the filename); only
     the name and language are hard requirements. */
  if (info.original_name[0] && info.original_language[0])
    info.error = 0;
  else
    fprintf(stderr, "Error: TMDB response missing required fields.\n");

  return info;
}

TmdbEpisodeInfo tmdb_fetch_episode(int tmdb_id, int season, int episode) {
  TmdbEpisodeInfo info = {.error = -1};
  info.name[0] = '\0';

  char path[96];
  snprintf(path, sizeof(path), "tv/%d/season/%d/episode/%d", tmdb_id, season, episode);
  cJSON *json = tmdb_get_json(path);
  if (!json)
    return info;

  copy_json_string(json, "name", info.name, sizeof(info.name));
  cJSON_Delete(json);

  if (info.name[0])
    info.error = 0;
  return info;
}
```

Note: `sscanf(item->valuestring, "%d-", &year)` deliberately mirrors the pre-existing call shape in this file (it passes the current lint gates); do not "improve" it to strtol here.

- [ ] **Step 3: Build**

Run: `cmake --build build`
Expected: success, no new warnings.

- [ ] **Step 4: Commit**

```bash
git add include/vmavificient/tmdb.h src/tmdb/tmdb.c
git commit -m "feat(tmdb): TV series + episode fetchers, shared GET helper

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 4: CLI flags + main.c TV wiring

**Files:**
- Modify: `src/main/main.c` (usage text ~line 75, prompt helpers ~line 281, CLI vars ~line 575, option enum ~line 635, `long_options` ~line 689, switch ~line 730, validation ~line 883, naming setup ~lines 1252–1406, companion-HD ~line 2054)

- [ ] **Step 1: Usage text**

In `print_usage()`, directly after the `--tmdb <id>` line (~line 75), insert:

```c
          "  --tv             TV mode: --tmdb <id> is a TMDB series ID\n"
          "                   (themoviedb.org/tv/<id>). Output is named\n"
          "                   Show.SxxEyy.Episode.Title.<...> — no year.\n"
          "  --mv             Movie mode (the default; explicit form)\n"
          "  --season <N>     Season number (with --tv; overrides filename\n"
          "                   parsing, prompts if still unknown)\n"
          "  --episode <N>    Episode number (with --tv; same resolution\n"
          "                   order as --season)\n"
```

- [ ] **Step 2: Prompt helper**

After the closing brace of `ask_source()` (~line 281), insert:

```c
/**
 * @brief Prompt for a positive integer on stdin (fgets + parse, no scanf).
 * @return 0 on success, -1 on EOF/non-interactive stdin or 3 bad answers.
 */
static int ask_positive_int(const char *prompt, int *out) {
  char line[16];
  for (int tries = 0; tries < 3; tries++) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin))
      return -1;
    int v = parse_int_or_zero(line);
    if (v > 0) {
      *out = v;
      return 0;
    }
    printf("Please enter a positive number.\n");
  }
  return -1;
}
```

- [ ] **Step 3: CLI variables**

After `SourceType cli_source = SOURCE_UNKNOWN;` (~line 572), insert:

```c
  bool tv_mode = false;
  int cli_season = 0;  /* 0 = resolve from filename, then prompt */
  int cli_episode = 0; /* same resolution order as cli_season */
```

- [ ] **Step 4: Option enum + long_options + switch cases**

In the option enum, after `OPT_CACHE_DIR,` (~line 635), insert:

```c
    OPT_TV,
    OPT_MV,
    OPT_SEASON,
    OPT_EPISODE,
```

In `long_options[]`, before the `{0, 0, 0, 0},` terminator (~line 690), insert:

```c
      {"tv", no_argument, 0, OPT_TV},
      {"mv", no_argument, 0, OPT_MV},
      {"season", required_argument, 0, OPT_SEASON},
      {"episode", required_argument, 0, OPT_EPISODE},
```

(getopt_long prefers exact matches, so `--tv` is unambiguous next to `--tvrip`.)

In the switch, after the `OPT_CACHE_DIR` case's `break;` (~line 862), insert:

```c
    case OPT_TV:
      tv_mode = true;
      break;
    case OPT_MV:
      tv_mode = false;
      break;
    case OPT_SEASON:
      cli_season = parse_int_or_zero(optarg);
      if (cli_season <= 0) {
        fprintf(stderr, "Error: --season must be a positive integer\n");
        return 1;
      }
      break;
    case OPT_EPISODE:
      cli_episode = parse_int_or_zero(optarg);
      if (cli_episode <= 0) {
        fprintf(stderr, "Error: --episode must be a positive integer\n");
        return 1;
      }
      break;
```

- [ ] **Step 5: Flag validation**

After the `--companion-hd`/`--scale-to-hd` mutual-exclusion check (~line 882), insert:

```c
  if (!tv_mode && (cli_season > 0 || cli_episode > 0)) {
    fprintf(stderr, "Error: --season/--episode require --tv\n");
    return 1;
  }
```

- [ ] **Step 6: Saved-episode state for companion-HD**

After `int saved_tmdb_year = 0;` (~line 1254), insert:

```c
  EpisodeInfo saved_episode = {0};
  bool saved_is_tv = false;
```

- [ ] **Step 7: Restructure the TMDB naming branch**

Replace the **entire** `} else if (tmdb_id > 0) { ... }` block (currently lines 1286–1406, from `} else if (tmdb_id > 0) {` through the closing `}` that directly precedes `if (naming_ok) {`) with the block below. The inner logic (source prompt, French variant, language tag, `FrenchAudioOrigin`, output dir) is carried over verbatim, only re-pointed from `tmdb.*` fields to the `meta_*` locals:

```c
  } else if (tmdb_id > 0) {
    /* Common metadata, filled from the movie or TV endpoint. */
    char meta_title[512] = "";
    int meta_year = 0;
    char meta_lang[8] = "";
    bool meta_ok = false;
    EpisodeInfo ep = {0};

    if (tv_mode) {
      /* S/E resolution: flags > filename parse > interactive prompt. */
      int season = cli_season;
      int episode = cli_episode;
      if (season <= 0 || episode <= 0) {
        const char *fname = strrchr(filepath, '/');
        fname = fname ? fname + 1 : filepath;
        int ps = 0, pe = 0;
        if (parse_season_episode(fname, &ps, &pe) == 0) {
          if (season <= 0)
            season = ps;
          if (episode <= 0)
            episode = pe;
        }
      }
      if (season <= 0 &&
          ask_positive_int("\nSeason not detected from filename. Season: ", &season) != 0) {
        ui_stage_fail("Naming", "season unknown; pass --season <N>");
        return 1;
      }
      if (episode <= 0 &&
          ask_positive_int("Episode not detected from filename. Episode: ", &episode) != 0) {
        ui_stage_fail("Naming", "episode unknown; pass --episode <N>");
        return 1;
      }

      ui_section("TMDB lookup");
      ui_kv("TV ID", "%d", tmdb_id);
      ui_kv("Episode", "S%02dE%02d", season, episode);
      TmdbTvInfo tv = tmdb_fetch_tv(tmdb_id);
      if (tv.error != 0) {
        ui_stage_fail("TMDB fetch", "could not fetch TV show info");
        ui_hint("verify TMDB_API_KEY is set in config.ini and the ID is a "
                "TV series ID (tmdb.org/tv/<id>, not /movie/)");
      } else {
        snprintf(meta_title, sizeof(meta_title), "%s", tv.original_name);
        meta_year = tv.first_air_year;
        snprintf(meta_lang, sizeof(meta_lang), "%s", tv.original_language);
        ep.season = season;
        ep.episode = episode;
        TmdbEpisodeInfo epi = tmdb_fetch_episode(tmdb_id, season, episode);
        if (epi.error == 0)
          snprintf(ep.title, sizeof(ep.title), "%s", epi.name);
        else
          ui_hint("episode title unavailable on TMDB; filename will omit it");
        meta_ok = true;
      }
    } else {
      ui_section("TMDB lookup");
      ui_kv("Movie ID", "%d", tmdb_id);
      TmdbMovieInfo tmdb = tmdb_fetch_movie(tmdb_id);
      if (tmdb.error != 0) {
        ui_stage_fail("TMDB fetch", "could not fetch movie info");
        ui_hint("verify TMDB_API_KEY is set in config.ini and the ID is "
                "correct (e.g. tmdb.org/movie/<id>)");
      } else {
        snprintf(meta_title, sizeof(meta_title), "%s", tmdb.original_title);
        meta_year = tmdb.release_year;
        snprintf(meta_lang, sizeof(meta_lang), "%s", tmdb.original_language);
        meta_ok = true;
      }
    }

    if (meta_ok) {
      ui_kv("Title", "%s", meta_title);
      if (tv_mode) {
        if (ep.title[0])
          ui_kv("Episode title", "%s", ep.title);
      } else {
        ui_kv("Year", "%d", meta_year);
      }
      ui_kv("Language", "%s", meta_lang);

      /* Source: CLI flag > filename detection > interactive prompt. */
      if (source == SOURCE_UNKNOWN)
        source = detect_source_from_filename(filepath);
      if (source == SOURCE_UNKNOWN)
        source = ask_source();

      /* Determine French variant for OPUS naming. */
      bool has_french = false;
      if (tracks.error == 0) {
        for (int i = 0; i < tracks.audio_count; i++) {
          if (strcmp(tracks.audio[i].language, "fre") == 0 ||
              strcmp(tracks.audio[i].language, "fra") == 0) {
            has_french = true;
            break;
          }
        }
      }
      if (has_french)
        fv = detect_french_variant_from_filename(filepath);

      /* Language: CLI flag > auto-detection > interactive prompt. */
      LanguageTag lang_tag;
      if (cli_lang_tag != LANG_TAG_NONE) {
        lang_tag = cli_lang_tag;
      } else {
        LanguageTag auto_tag = determine_language_tag(&tracks, meta_lang, fv);

        /* If auto-detection produced a definitive result, use it.
           Otherwise ask the user interactively. */
        if (auto_tag != LANG_TAG_VO || tracks.audio_count <= 1) {
          lang_tag = auto_tag;
        } else {
          lang_tag = ask_language_tag(&tracks);
        }
      }
      resolved_lang_tag = lang_tag;

      snprintf(saved_tmdb_title, sizeof(saved_tmdb_title), "%s", meta_title);
      saved_tmdb_year = meta_year;
      saved_episode = ep;
      saved_is_tv = tv_mode;

      build_output_filename(output_name, sizeof(output_name), meta_title, meta_year, lang_tag,
                            &info, &hdr, source, tv_mode ? &ep : NULL);

      /* Strip .mkv to get base name. */
      snprintf(base_name, sizeof(base_name), "%s", output_name);
      char *ext = strrchr(base_name, '.');
      if (ext && strcmp(ext, ".mkv") == 0)
        *ext = '\0';

      /* Output dir: same directory as input file. */
      snprintf(output_dir, sizeof(output_dir), "%s", filepath);
      char *last_slash = strrchr(output_dir, '/');
      if (last_slash)
        *(last_slash + 1) = '\0';
      else
        snprintf(output_dir, sizeof(output_dir), "./");

      /* ---- Resolve FrenchAudioOrigin ----
         The CLI language tag wins over the filename-derived French variant
         so that e.g. --multivfi on a source with no VFI marker still labels
         tracks as VFI. */
      switch (lang_tag) {
      case LANG_TAG_MULTI_VFI:
      case LANG_TAG_DUAL_VFI:
        fv = FRENCH_VARIANT_VFI;
        break;
      case LANG_TAG_MULTI_VFQ:
      case LANG_TAG_DUAL_VFQ:
        fv = FRENCH_VARIANT_VFQ;
        break;
      case LANG_TAG_MULTI_VFF:
      case LANG_TAG_DUAL_VFF:
      case LANG_TAG_VFF:
      case LANG_TAG_TRUEFRENCH:
      case LANG_TAG_FRENCH:
        fv = FRENCH_VARIANT_VFF;
        break;
      default:
        /* Keep filename-detected fv as-is for MULTI / VO / VOST / etc. */
        break;
      }

      if (strcmp(meta_lang, "fr") == 0) {
        fr_audio_origin = FRENCH_AUDIO_VO;
      } else {
        switch (fv) {
        case FRENCH_VARIANT_VFQ:
          fr_audio_origin = FRENCH_AUDIO_VFQ;
          break;
        case FRENCH_VARIANT_VFI:
          fr_audio_origin = FRENCH_AUDIO_VFI;
          break;
        default:
          fr_audio_origin = FRENCH_AUDIO_VFF;
          break;
        }
      }

      if (tv_mode) {
        if (ep.title[0])
          snprintf(mkv_title, sizeof(mkv_title), "%s - S%02dE%02d - %s", meta_title, ep.season,
                   ep.episode, ep.title);
        else
          snprintf(mkv_title, sizeof(mkv_title), "%s - S%02dE%02d", meta_title, ep.season,
                   ep.episode);
      } else {
        snprintf(mkv_title, sizeof(mkv_title), "%s (%d)", meta_title, meta_year);
      }
      {
        const char *vlang = iso639_1_to_2b(meta_lang);
        if (vlang)
          video_language = vlang;
      }

      naming_ok = true;
    }
  }
```

- [ ] **Step 8: Companion-HD call site carries the episode**

At the Task-2-edited call (~line 2054), change the trailing `NULL` to:

```c
          build_output_filename(hd_output_name, sizeof(hd_output_name), saved_tmdb_title,
                                saved_tmdb_year, resolved_lang_tag, &hd_info, &hd_hdr, source,
                                saved_is_tv ? &saved_episode : NULL);
```

- [ ] **Step 9: Build + smoke-check flags**

Run: `cmake --build build`
Expected: success.
Run: `./build/vmavificient --help | grep -A2 -- --tv` — new flags listed.
Run: `./build/vmavificient --season 1 nonexistent.mkv; echo "exit=$?"`
Expected: `Error: --season/--episode require --tv`, exit=1.

- [ ] **Step 10: Run unit tests (regression)**

Run: `./build/vmav_tests`
Expected: `All tests passed`.

- [ ] **Step 11: Commit**

```bash
git add src/main/main.c
git commit -m "feat(main): --tv mode with season/episode resolution and TV naming

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 5: README

**Files:**
- Modify: `README.md` (options excerpt ~line 67, usage examples section ~line 175)

- [ ] **Step 1: Options excerpt**

After the `--tmdb <id>` line (~line 67), insert (match the surrounding column alignment):

```
--tv             TV mode: --tmdb <id> is a TMDB series ID (tmdb.org/tv/<id>)
--season <N>     Season number (with --tv; else parsed from filename, then prompted)
--episode <N>    Episode number (with --tv; else parsed from filename, then prompted)
```

- [ ] **Step 2: Usage example**

In the `## Usage` section, after the movie example (`./build/vmavificient --tmdb 335984 input.mkv`, ~line 179), add with a short comment line matching the style of the neighboring examples:

```
./build/vmavificient --tv --tmdb 890 "Neon Genesis Evangelion - Episode 01.mkv" --season 1 --episode 1
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): document --tv / --season / --episode

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 6: Validation on real content

**Files:** none (verification only). Uses `Neon Genesis Evangelion - Episode 01.mkv` in the repo root (untracked test media — do not stage it).

- [ ] **Step 1: Unit tests**

Run: `cmake --build build --target vmav_tests && ./build/vmav_tests`
Expected: `All tests passed`.

- [ ] **Step 2: TV dry-run with explicit S/E (TMDB TV ID 890 = Neon Genesis Evangelion)**

Run:
```bash
./build/vmavificient --tv --tmdb 890 --season 1 --episode 1 --dry-run --verbose "Neon Genesis Evangelion - Episode 01.mkv"
```
Expected: TMDB lookup section shows `TV ID 890`, `Episode S01E01`, an episode title, and the plan block shows an output name of the form `<show>.S01E01.<episode title>.<LANG>.1080p.<...>.10bit.AV1.OPUS-<group>.mkv` with **no year**. Note: `original_name` for this show is Japanese script — same semantics as movies' `original_title`; verify it sanitizes into the filename without crashing, and eyeball the result.

- [ ] **Step 3: Prompt fallback path**

Run the same command **without** `--season/--episode`; the filename has no `SxxEyy`, so both prompts must appear. Answer `1` and `1`; expect the same plan as Step 2. Then run once more with stdin closed:
```bash
./build/vmavificient --tv --tmdb 890 --dry-run "Neon Genesis Evangelion - Episode 01.mkv" < /dev/null; echo "exit=$?"
```
Expected: clean failure mentioning `--season <N>`, exit=1 — no hang, no crash.

- [ ] **Step 4: Movie regression**

Run:
```bash
./build/vmavificient --tmdb 335984 --dry-run "Neon Genesis Evangelion - Episode 01.mkv"
```
Expected: movie-format name `Blade.Runner.2049.2017....mkv` — identical shape to pre-feature output (title.year first, no SxxEyy). Also run `--blind --dry-run` on the same file: unchanged `<input-stem>.mkv` naming.

- [ ] **Step 5: Wrong-endpoint error path**

Run: `./build/vmavificient --tv --tmdb 335984 --season 1 --episode 1 --dry-run "Neon Genesis Evangelion - Episode 01.mkv"`
(335984 is a movie ID.) Expected: TMDB 404 + the hint pointing at `tmdb.org/tv/<id>, not /movie/`.

- [ ] **Step 6: Full encode (user's standing validation rule)**

Per the repo's validation practice, a real encode should confirm the mux: run the Step-2 command without `--dry-run` (or on a short clip if preferred), then `ffprobe` the output (streams, chapters, keyframe count) and scrub the timeline in a player. Check the MKV container title reads `<Show> - S01E01 - <Episode Title>`. This step is long-running — coordinate with the user before starting it.
