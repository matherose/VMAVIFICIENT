# TV Show Support — Design

**Date:** 2026-07-05
**Status:** Approved (v1 scope)

## Goal

Encode a single TV episode through the existing pipeline with correct
TMDB metadata and scene-convention naming. Season batch processing is
explicitly out of scope for v1 and designed as a follow-up.

## Decisions

| Question | Decision |
|----------|----------|
| Season/batch input | Single file only in v1; batch is a follow-up |
| Output name format | `Show.Title.S01E05.Episode.Title.<LANG>.<RES>.<FEATURE>.<SOURCE>.<QUALITY>.10bit.AV1.OPUS-<group>.mkv` — no year, episode title included |
| S/E resolution | `--season`/`--episode` flags → filename parse → interactive prompt (TTY, not `--quiet`) → hard error naming the flags |
| Architecture | Unified naming: `build_output_filename()` gains an optional `EpisodeInfo *` parameter (`NULL` = movie, byte-for-byte unchanged output) |

## 1. CLI

- `--tv` — TV mode: `--tmdb <id>` is interpreted as a TMDB **series** ID
  (`themoviedb.org/tv/<id>`).
- `--mv` — explicit movie mode (the default; accepted, documented, no-op).
- `--season <N>`, `--episode <N>` — S/E overrides; valid only with `--tv`.
- `--tv --blind` — blind behavior unchanged (input-stem naming, no TMDB).

Error paths:
- `--season`/`--episode` without `--tv` → usage error.
- `--tv` without `--tmdb` and without `--blind` → same behavior as movie
  mode today (no new rule).
- `--tv` with a movie ID → TMDB 404 surfaces a hint that the ID must come
  from `/tv/`, not `/movie/`.

## 2. TMDB module (`src/tmdb/`)

- Extract the curl-GET-JSON boilerplate currently inline in
  `tmdb_fetch_movie()` into a static helper shared by all fetchers.
- `tmdb_fetch_tv(int id)` → `GET /3/tv/{id}` →
  `TmdbTvInfo { error, original_name[512], first_air_year, original_language[8] }`
  — the exact analog of `TmdbMovieInfo`, so `determine_language_tag()`
  works unchanged.
- `tmdb_fetch_episode(int id, int season, int episode)` →
  `GET /3/tv/{id}/season/{s}/episode/{e}` → episode `name`.
  **Non-fatal on failure**: a missing episode title (special, unaired,
  TMDB gap) prints a warning and the filename omits that segment; it never
  blocks an encode. Episode names from TMDB are localized (default
  English); the show title uses `original_name`, mirroring the movie
  path's use of `original_title`.

## 3. Episode parsing (`src/media_naming/`)

- `int parse_season_episode(const char *filename, int *season, int *episode)`
  — returns 0 on success, non-zero on failure. Patterns in priority order:
  1. `SxxEyy` — any case, optional separator between S and E groups
     (`S01E05`, `s01e05`, `S01.E05`).
  2. `NxNN` — guarded fallback: match must be separator-bounded and
     rejected when the values look like a resolution (e.g. second value
     ≥ 100), so `1920x1080` never parses as season 1920 episode 1080.
- Known v1 limitation (documented): double episodes (`S01E05E06`) take
  the first episode number.
- Interactive prompt uses `fgets` + `strtol` (never scanf-family;
  cert-err33/34 gates apply), validates positive integers, re-asks on
  garbage input.

## 4. Naming (`media_naming.h` / `src/media_naming/`)

- New struct:
  ```c
  typedef struct {
    int season;
    int episode;
    char title[256]; /* episode title; empty string = omit segment */
  } EpisodeInfo;
  ```
- `build_output_filename(..., const EpisodeInfo *ep)`:
  - `ep == NULL` → movie format, byte-for-byte identical to today.
  - `ep != NULL` → `Show.Title.SxxEyy[.Episode.Title].LANG.RES.FEATURE.SOURCE.QUALITY.10bit.AV1.OPUS-<group>.mkv`
    — year omitted for TV; episode title sanitized through the existing
    `sanitize_title()`.
- MKV container title: movie stays `Title (Year)`; TV becomes
  `Show Title - S01E05 - Episode Title` (title segment dropped if empty).
- Companion-HD path: the `saved_tmdb_title` mechanism also saves the
  `EpisodeInfo` so the 1080p companion of a 4K episode is named correctly.

## 5. Unchanged

Pipeline stages (Crop → Grain → CRF → Audio → Subs → Video → Mux),
`state.json`, cache handling, language/source/variant detection from the
input filename, quality presets. This is a metadata-and-naming feature
only.

## 6. Testing

- Unit tests:
  - `parse_season_episode` pattern matrix, including negatives:
    resolution strings (`1920x1080`, `1080p`), source tags, and plain
    movie names must not parse.
  - TV filename assembly (with and without episode title).
  - Movie filename regression (NULL episode → identical output).
- Real-content validation (standing rule): full run on an actual episode
  file with `--verbose`, ffprobe checks (streams, chapters, keyframe
  count), timeline scrub in a player, before declaring done.

## 7. Follow-up (out of scope, shaped by this design)

- **Season batch mode**: loop the pipeline over N files with shared
  `TmdbTvInfo`, per-file cache/state, per-episode `EpisodeInfo`. The
  unified metadata struct is what makes this loop trivial.
- Decision deferred to that effort: per-episode CRF search vs. reusing
  E01's result across the season.
- Coordination note: `refactor/modular-architecture` Step 7 ("thin
  main") will eventually absorb the naming section this feature extends
  in `main.c`.
