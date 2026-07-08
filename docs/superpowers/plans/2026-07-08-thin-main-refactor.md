# Thin-Main Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shrink `src/main/main.c` from 2,538 lines to a thin driver (< 250 lines) by extracting the pipeline stages into a new `src/pipeline/` module, and unify the duplicated 4K / HD encode blocks into one parameterized pass — with **zero behavior change**.

**Architecture:** A heap-allocated `PipelineCtx` struct carries all state between stages. Each stage is a function `StageStatus stage_x(PipelineCtx *ctx)` that returns *continue / exit-ok / exit-fail*, mirroring today's early `return` points exactly. The near-duplicate 4K and HD encode+mux blocks collapse into `stage_rate_plan()` + `encode_pass()` driven by an `EncodePassParams` struct.

**Tech Stack:** C11, CMake + Ninja, existing module conventions (source in `src/<module>/`, public header in `include/vmavificient/`).

---

## Non-negotiable ground rules (repo conventions)

1. **This is code motion, not improvement.** Move code byte-for-byte wherever possible. Do NOT fix style, rename variables, reorder UI output, change error messages, or "clean up" logic. Any observable output difference is a bug in the refactor. (Known pre-existing quirks to preserve untouched: `best` from `select_best_audio_per_language()` in the tracks-display block is never freed; the chroma-score kv prints an approximation. Leave both alone.)
2. **Commit trailer:** end every commit message with
   `Assisted-by: Claude Fable 5 <noreply@anthropic.com>` (never Co-Authored-By).
3. **`git add` explicit paths only. NEVER `git add -A` / `git add .`** — the repo root contains huge untracked media files.
4. **Format only with** `uvx --from clang-format==18.1.8 clang-format -i <files>` (local clang-format is v22 and produces different output — never use it).
5. Build: `cmake --build build`. Tests: `./build/vmav_tests`. The `build/` tree uses Homebrew deps (`VMAV_USE_SYSTEM_DEPS=ON`) and is the right one for iteration.
6. All `vmavificient` pipeline invocations run in the background (`nohup … & disown` or the harness's background mode) — never block a foreground shell on an encode.
7. Line numbers cited below are valid **against commit `edaa792`'s `src/main/main.c`** and shift after each task. Locate blocks by the quoted section-comment anchors, not by absolute line numbers.

## Test fixture (already provisioned)

`$SCRATCH` = the session scratchpad directory. Fixture: `$SCRATCH/muxtest/` with the 60 s clip `$SCRATCH/muxsrc/Neon Genesis Evangelion - Episode 01.mkv` (332 MB), cache dir `$SCRATCH/muxtest/cache/` whose `scores.json` is seeded with `"crf": 35` so runs skip CRF search. TMDB ID 890 (NGE) with `--tv --season 1 --episode 1` produces a real rename → the final mux actually runs (in `--blind` mode the output name collides with the source and the mux is skipped — never use `--blind` to validate the mux path).

---

### Task 0: Capture the behavior baseline (no code changes)

**Files:** none in-repo; artifacts under `$SCRATCH/thinmain-baseline/`.

- [ ] **Step 1: Capture the dry-run transcript** (fast; exercises probe → grain → naming → CRF-cache → plan):

```bash
mkdir -p "$SCRATCH/thinmain-baseline"
cd "$SCRATCH/muxtest"
BIN=/Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/build/vmavificient
"$BIN" --tmdb 890 --tv --season 1 --episode 1 --dry-run --cache-dir cache \
  "../muxsrc/Neon Genesis Evangelion - Episode 01.mkv" \
  > "$SCRATCH/thinmain-baseline/dryrun.txt" 2>&1; echo "RC=$?" >> "$SCRATCH/thinmain-baseline/dryrun.txt"
```

Expected: `RC=0` and the transcript ends with the "Dry run / No files written" section.

- [ ] **Step 2: Capture a grain-only transcript** (exercises `print_encoder_knobs`):

```bash
"$BIN" --tmdb 890 --tv --season 1 --episode 1 --grain-only --cache-dir cache \
  "../muxsrc/Neon Genesis Evangelion - Episode 01.mkv" \
  > "$SCRATCH/thinmain-baseline/grainonly.txt" 2>&1; echo "RC=$?" >> "$SCRATCH/thinmain-baseline/grainonly.txt"
```

- [ ] **Step 3: Full encode+mux baseline, run in background** (~a few minutes for the 60 s clip). Delete any previous output first so the mux is not skipped:

```bash
rm -f "$SCRATCH/muxsrc/"新世紀エヴァンゲリオン.S01E01.*.mkv
nohup bash -c 'cd "$SCRATCH/muxtest" && "'$BIN'" --tmdb 890 --tv --season 1 --episode 1 --cache-dir cache \
  "../muxsrc/Neon Genesis Evangelion - Episode 01.mkv"; echo "RC=$?"' \
  > "$SCRATCH/thinmain-baseline/encode1.log" 2>&1 & disown
```

Wait for `RC=` in the log; expect `RC=0`.

- [ ] **Step 4: Record the output's stream layout + per-stream hashes:**

```bash
OUT=$(ls "$SCRATCH/muxsrc/"新世紀エヴァンゲリオン.S01E01.*.mkv)
ffprobe -v error -show_entries "stream=index,codec_name,codec_type,channels:stream_tags=language,title:format_tags=title" \
  -of json "$OUT" > "$SCRATCH/thinmain-baseline/layout.json"
NSTREAMS=$(ffprobe -v error -show_entries stream=index -of csv=p=0 "$OUT" | wc -l)
for i in $(seq 0 $((NSTREAMS-1))); do
  ffmpeg -v error -i "$OUT" -map 0:$i -c copy -f md5 - 2>/dev/null
done > "$SCRATCH/thinmain-baseline/stream-md5s.txt"
mkvextract "$OUT" chapters "$SCRATCH/thinmain-baseline/chapters.xml" 2>/dev/null || \
  ffprobe -v error -show_chapters -of json "$OUT" > "$SCRATCH/thinmain-baseline/chapters.json"
mv "$OUT" "$SCRATCH/thinmain-baseline/baseline-output.mkv"
```

- [ ] **Step 5: Determinism check — run the encode a second time (Step 3 command again), hash again, and diff:** streams whose md5 differs between the two baseline runs (possibly the AV1 video stream — the encoder is threaded) are excluded from future md5 comparison; compare those by `nb_read_packets` + keyframe count instead:

```bash
ffprobe -v error -count_packets -select_streams v:0 \
  -show_entries stream=nb_read_packets -of csv=p=0 file.mkv          # packet count
ffprobe -v error -skip_frame nokey -select_streams v:0 -count_frames \
  -show_entries stream=nb_read_frames -of csv=p=0 file.mkv           # keyframe count
```

Record the verdict (which streams are deterministic) in `$SCRATCH/thinmain-baseline/NOTES.txt`. Move the second output aside as well.

**Done when:** `dryrun.txt`, `grainonly.txt`, `layout.json`, `stream-md5s.txt`, chapters dump, `NOTES.txt`, and both baseline outputs exist.

---

### Task 1: Scaffold `pipeline.h` + shared utils module

**Files:**
- Create: `include/vmavificient/pipeline.h`
- Create: `src/pipeline/pipeline_util.c`
- Modify: `src/main/main.c` (delete moved helpers, include new header, adapt call sites)
- Modify: `CMakeLists.txt` (register `src/pipeline/pipeline_util.c` in the `add_executable(vmavificient …)` list, alphabetically after `src/media_tracks/media_tracks.c`)

- [ ] **Step 1: Write `include/vmavificient/pipeline.h`** — the complete contract for every later task:

```c
/**
 * @file pipeline.h
 * @brief Shared pipeline context and stage entry points for the
 *        vmavificient driver (src/pipeline/).
 */
#ifndef VMAV_PIPELINE_H
#define VMAV_PIPELINE_H

#include <stdbool.h>
#include <time.h>

#include "crf_search.h"
#include "encode_preset.h"
#include "media_analysis.h"
#include "media_crop.h"
#include "media_hdr.h"
#include "media_info.h"
#include "media_naming.h"
#include "media_tracks.h"

/* How a stage tells the driver to proceed. Mirrors the early-return
   points of the original monolithic main(). */
typedef enum {
  STAGE_CONTINUE = 0, /* keep running the pipeline           */
  STAGE_EXIT_OK,      /* stop now, process exit code 0        */
  STAGE_EXIT_FAIL,    /* stop now, process exit code 1        */
} StageStatus;

/* Everything the CLI parser resolves. Field names match the old
   main() locals (cli_ prefix dropped) so moved code reads naturally. */
typedef struct {
  int tmdb_id;
  int bitrate;     /* 0 = run crf-search (default path) */
  int crf;         /* 0 = run crf-search; >0 = use directly */
  int vmaf_target; /* 0 = per-preset default */
  bool blind;
  bool dry_run;
  bool quiet;
  bool verbose;
  bool grain_only;
  bool companion_hd;
  bool scale_to_hd;
  bool tv_mode;
  int season;  /* 0 = resolve from filename, then prompt */
  int episode; /* same resolution order as season */
  char cache_dir[4096]; /* "" = default ./.vmavificient-cache */
  LanguageTag lang_tag;
  SourceType source;
  QualityType quality;
  const char *extra_srt_paths[16];
  int extra_srt_count;
  const char *filepath; /* positional input (or DEFAULT_TEST_FILE) */
} VmavOptions;

/* Per-pass knobs for the unified rate-control/plan + encode/mux pass.
   The 4K pass and the HD (companion / scale-to-hd) pass differ only
   in these fields. */
typedef struct {
  const EncodePreset *preset;
  int film_grain;
  int crf;       /* in: cli override or 0; out: resolved by rate_plan */
  int vmaf_used; /* out: resolved by rate_plan */
  const char *base_name;   /* stem for cache-file names (RPU, video.mkv) */
  const char *output_name; /* final .mkv filename */
  int scale_width;  /* 0 = encode at source resolution (4K pass) */
  int scale_height; /* 0 = encode at source resolution */
  const char *crf_scale_filter; /* NULL, or "scale=1920:1080:flags=lanczos" */
  const MediaInfo *plan_info;   /* dims shown in the plan (hd_info for HD) */
  const HdrInfo *enc_hdr;       /* hdr passed to the video encoder */
  bool is_hd;             /* HD pass: "HD …" labels when companion_hd,
                             Scale kv in plan, no bitrate-delta receipt */
  bool use_cached_crf;    /* 4K pass only: honor cached_crf from scores */
  bool dry_run_falls_through; /* 4K pass with --companion-hd: print plan,
                                 do not exit, so the HD plan also renders */
  bool extract_rpu;       /* extract RPU in this pass (4K pass, or HD
                             pass when --scale-to-hd) */
  bool defer_shared_cleanup; /* 4K pass with --companion-hd: keep opus/
                                srt/rpu intermediates for the HD mux */
} EncodePassParams;

/* All cross-stage state. Heap-allocate with calloc(1, sizeof) — the
   embedded path arrays total several hundred kB. */
typedef struct {
  VmavOptions opt;

  char cache_dir[4096]; /* resolved (opt.cache_dir or default) */
  char scores_cache_path[4096];
  double cached_grain_score;
  double cached_grain_variance;
  int cached_crf;
  bool scores_cached;

  MediaInfo info;
  HdrInfo hdr;
  CropInfo crop;
  MediaTracks tracks;

  GrainScore grain;
  int film_grain;
  const EncodePreset *enc_preset;

  /* Naming outputs. */
  char output_name[1024];
  char base_name[1024];
  char output_dir[2048];
  char mkv_title[1024];
  char saved_tmdb_title[512];
  int saved_tmdb_year;
  EpisodeInfo saved_episode;
  bool saved_is_tv;
  const char *video_language;
  SourceType source;
  FrenchVariant fv;
  FrenchAudioOrigin fr_audio_origin;
  LanguageTag resolved_lang_tag;

  /* Audio stage outputs. */
  char opus_paths[32][4096];
  char audio_names[32][256];
  char audio_langs[32][16];
  int opus_count;
  int audio_fail_count;

  /* Subtitle stage outputs. */
  char srt_paths[64][4096];
  char srt_names[64][256];
  char srt_langs[64][64];
  int srt_is_forced[64];
  int srt_is_sdh[64];
  int srt_variant[64];
  int srt_count;

  char rpu_path[4096];

  time_t encode_start_time;
  int pipeline_failed; /* drives the process exit code */
} PipelineCtx;

/* ---- pipeline_util.c ---- */
int vmav_parse_int_or_zero(const char *s);
bool vmav_file_exists(const char *path);
const char *vmav_codec_short(const char *codec);
void vmav_build_cache_path(const PipelineCtx *ctx, char *buf, size_t bufsize,
                           const char *relative_path);
void vmav_cleanup_cache_dir(const PipelineCtx *ctx);
bool vmav_load_cached_scores(const char *cache_path, double *grain_score,
                             double *grain_variance, int *crf);
bool vmav_save_cached_scores(const char *cache_path, double grain_score,
                             double grain_variance, int crf);
int vmav_audio_lang_priority(const char *lang);
int vmav_cmp_audio_order(const void *a, const void *b);
int vmav_sub_sort_key(const char *lang, int is_forced);
void vmav_print_encoder_knobs(const EncodePreset *p, int film_grain);

/* ---- cli.c (Task 2) ---- */
void vmav_print_usage(const char *prog);
/* Pre-getopt scan for --help/--config/--blind (they must act before
   config_init). Returns like a mini-main: -1 = keep going, >=0 = exit
   now with that code. Sets opt->blind. */
int vmav_cli_prescan(int argc, char *argv[], VmavOptions *opt);
/* Full getopt_long pass. Returns -1 = keep going, >=0 = exit code. */
int vmav_cli_parse(int argc, char *argv[], VmavOptions *opt);

/* ---- stage_probe.c (Task 3) ---- */
StageStatus stage_probe(PipelineCtx *ctx);

/* ---- stage_grain.c (Task 4) ---- */
StageStatus stage_grain(PipelineCtx *ctx);

/* ---- stage_naming.c (Task 5) ---- */
StageStatus stage_naming(PipelineCtx *ctx);

/* ---- stage_audio.c (Task 6) ---- */
StageStatus stage_audio(PipelineCtx *ctx);

/* ---- stage_subs.c (Task 7) ---- */
StageStatus stage_subs(PipelineCtx *ctx);

/* ---- encode_pass.c (Tasks 8-9) ---- */
StageStatus stage_rate_plan(PipelineCtx *ctx, EncodePassParams *pass);
StageStatus encode_pass(PipelineCtx *ctx, EncodePassParams *pass);

#endif /* VMAV_PIPELINE_H */
```

- [ ] **Step 2: Create `src/pipeline/pipeline_util.c`.** Move these functions from `main.c` **verbatim** (bodies unchanged), renamed per the header above: `parse_int_or_zero` → `vmav_parse_int_or_zero`; `file_exists` → `vmav_file_exists`; `codec_short` → `vmav_codec_short`; `load_cached_scores`/`save_cached_scores` → `vmav_`-prefixed; `audio_lang_priority`/`cmp_audio_order`/`sub_sort_key` → `vmav_`-prefixed (`vmav_cmp_audio_order` calls `vmav_audio_lang_priority`); `print_encoder_knobs` → `vmav_print_encoder_knobs`.
  Two functions change signature because the `g_cache_dir` file-scope global is retired: `build_cache_path` and `cleanup_cache_dir` become `vmav_build_cache_path(ctx, …)` / `vmav_cleanup_cache_dir(ctx)`, with every internal use of `g_cache_dir` replaced by `ctx->cache_dir`. Includes needed: `errno.h stdio.h stdlib.h string.h sys/stat.h time.h unistd.h limits.h` plus `"pipeline.h" "proc.h" "ui.h"`.
- [ ] **Step 3: Update `main.c`:** delete the moved functions and the `g_cache_dir` global; add `#include "pipeline.h"`; declare `PipelineCtx *ctx = calloc(1, sizeof(*ctx));` right after `ui_init()` (check for NULL → `return 1`); replace `g_cache_dir` with `ctx->cache_dir` and update every call site to the new names/signatures. For this task only, main keeps its existing flow — locals like `scores_cached` move to `ctx->scores_cached` etc. only where the moved helpers force it; wholesale migration happens per-stage in later tasks. Add `free(ctx)` before each `return` in main **or** simpler: `int rc = …; free(ctx); return rc;` — pick one pattern and use it at every exit point.
- [ ] **Step 4: Register the new source in CMake, build, test:**

```bash
cmake --build /Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/build
/Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/build/vmav_tests
```

Expected: clean build, all tests pass.
- [ ] **Step 5: Smoke — dry-run transcript must match baseline byte-for-byte:**

```bash
cd "$SCRATCH/muxtest"
"$BIN" --tmdb 890 --tv --season 1 --episode 1 --dry-run --cache-dir cache \
  "../muxsrc/Neon Genesis Evangelion - Episode 01.mkv" > /tmp_out.txt 2>&1; echo "RC=$?" >> /tmp_out.txt
diff "$SCRATCH/thinmain-baseline/dryrun.txt" /tmp_out.txt
```

(Write the temp file inside `$SCRATCH`, not literally `/tmp_out.txt`.) Expected: empty diff. Note: `scores.json` rewrites its `generated` timestamp on each run but that lands in the cache file, not stdout, so the transcript diff is exact.
- [ ] **Step 6: Format + commit:**

```bash
uvx --from clang-format==18.1.8 clang-format -i include/vmavificient/pipeline.h src/pipeline/pipeline_util.c src/main/main.c
cmake --build build && ./build/vmav_tests
git add include/vmavificient/pipeline.h src/pipeline/pipeline_util.c src/main/main.c CMakeLists.txt
git commit -m "refactor(main): extract shared pipeline helpers into src/pipeline"
```

---

### Task 2: Extract the CLI parser

**Files:**
- Create: `src/pipeline/cli.c`
- Modify: `src/main/main.c`, `CMakeLists.txt` (add `src/pipeline/cli.c`)

- [ ] **Step 1: Create `src/pipeline/cli.c`** containing, moved verbatim from `main.c`: `print_usage` (→ `vmav_print_usage`); the anonymous option `enum` (`OPT_TMDB` … `OPT_EPISODE`); the `static struct option long_options[]` table; the entire `getopt_long` loop plus post-loop validation (`--quiet`/`--verbose` application via `ui_set_quiet`/`ui_set_verbose`, the `companion_hd && scale_to_hd` and `--season/--episode require --tv` checks) wrapped as:

```c
int vmav_cli_parse(int argc, char *argv[], VmavOptions *opt) {
  /* moved getopt_long loop; every `cli_foo = x` becomes `opt->foo = x`,
     every `return 1` in an error branch becomes `return 1`, and the
     final fallthrough returns -1 (keep going). */
  ...
  if (optind < argc)
    opt->filepath = argv[optind];
  else
    opt->filepath = DEFAULT_TEST_FILE;
  return -1;
}
```

  and the pre-scan loop (`--help` / `--config` / `--blind` detection that currently sits above `config_init()`) as:

```c
int vmav_cli_prescan(int argc, char *argv[], VmavOptions *opt) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      vmav_print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--config") == 0)
      return config_interactive_setup();
    if (strcmp(argv[i], "--blind") == 0)
      opt->blind = true;
  }
  return -1;
}
```

  Inside the moved getopt loop the `OPT_HELP` / `OPT_CONFIG_SETUP` cases keep their current behavior (`vmav_print_usage(argv[0]); return 0;` and `return config_interactive_setup();`). `parse_int_or_zero` calls become `vmav_parse_int_or_zero`.
- [ ] **Step 2: Rewire `main.c`:**

```c
int prescan_rc = vmav_cli_prescan(argc, argv, &ctx->opt);
if (prescan_rc >= 0) { free(ctx); return prescan_rc; }
if (!ctx->opt.blind)
  config_init();
if (check_dependencies() != 0) { … }        /* unchanged */
int cli_rc = vmav_cli_parse(argc, argv, &ctx->opt);
if (cli_rc >= 0) { free(ctx); return cli_rc; }
int do_hd = ctx->opt.companion_hd || ctx->opt.scale_to_hd;
```

  Then replace every use of the old locals (`tmdb_id`, `cli_bitrate`, `cli_crf`, `cli_vmaf_target`, `dry_run`, `quiet`, `verbose`, `grain_only`, `companion_hd`, `scale_to_hd`, `cli_cache_dir`, `cli_lang_tag`, `cli_source`, `tv_mode`, `cli_season`, `cli_episode`, `cli_quality`, `extra_srt_paths`, `extra_srt_count`, `blind`, `filepath`) with `ctx->opt.<field>` throughout the remaining body of main. Also move the cache-dir resolution block into main as-is but writing `ctx->cache_dir`, and `scores_cache_path` → `ctx->scores_cache_path`.
- [ ] **Step 3: Build + tests** (same commands as Task 1 Step 4). Additionally verify flag error paths still behave:

```bash
./build/vmavificient --crf 99 nonexistent.mkv; echo "RC=$?"          # expect "Error: --crf must be in range 1–63", RC=1
./build/vmavificient --season 2 nonexistent.mkv < /dev/null; echo "RC=$?"  # expect "--season/--episode require --tv", RC=1
./build/vmavificient --help | head -3                                 # expect usage text
```

- [ ] **Step 4: Smoke — dry-run diff vs baseline (Task 1 Step 5). Expected: empty diff.**
- [ ] **Step 5: Format + commit** `refactor(main): extract CLI parsing into src/pipeline/cli.c`.

---

### Task 3: Extract the probe/report stage

**Files:**
- Create: `src/pipeline/stage_probe.c`
- Modify: `src/main/main.c`, `CMakeLists.txt`

- [ ] **Step 1: Create `stage_probe.c`** with `StageStatus stage_probe(PipelineCtx *ctx)`. Move verbatim from main, in order:
  - the `/* ---- Source ---- */` block (`get_media_info` + Source section; error → `STAGE_EXIT_FAIL`),
  - the `/* ---- Color (HDR) ---- */` block,
  - the `/* ---- Crop ---- */` block,
  - the `/* ---- Tracks ---- */` block **including** the audio table, `select_best_audio_per_language` display pass, subtitle table, PGS-skip precompute, and the "Processing" summary (this is the block ending just before `/* Early resume check … */`),
  - the early-resume check (the `video_4k_cache_path` / `video_hd_cache_path` computation and the two `file_exists` → `STAGE_EXIT_OK` branches; `file_exists` → `vmav_file_exists`, `g_cache_dir` already `ctx->cache_dir`),
  - the `/* ---- OCR preflight … ---- */` block (failure → `STAGE_EXIT_FAIL`).
  Results land in `ctx->info`, `ctx->hdr`, `ctx->crop`, `ctx->tracks`. The block-local `best`/`best_count` stay local to this function (preserving the existing non-free of `best`). `codec_short` → `vmav_codec_short`.
- [ ] **Step 2: Rewire main:**

```c
StageStatus st = stage_probe(ctx);
if (st != STAGE_CONTINUE) { rc = (st == STAGE_EXIT_FAIL) ? 1 : 0; goto out; }
```

  Introduce the single exit label now if not already present:

```c
out:
  if (ctx->tracks.error == 0)
    free_media_tracks(&ctx->tracks);
  free(ctx);
  return rc;
```

  **Careful:** today the probe-failure return happens *before* `tracks` is populated — `free_media_tracks` guarded by `tracks.error == 0` handles that safely because `calloc` zeroed the struct **only if** error 0 means "populated". `get_media_tracks` sets `.error`; a zeroed struct has `error == 0` with null pointers, and `free_media_tracks(NULL-fields)` must be safe — verify by reading `src/media_tracks/media_tracks.c::free_media_tracks`; if it doesn't tolerate zeroed contents, add `ctx->tracks.error = -1;` in `main()` right after `calloc` and rely on `stage_probe` to overwrite it.
- [ ] **Step 3: Build + tests.**
- [ ] **Step 4: Smoke — dry-run diff vs baseline. Expected: empty diff** (this task moves the biggest chunk of UI code; the byte-exact transcript is the whole point).
- [ ] **Step 5: Format + commit** `refactor(main): extract probe/report stage`.

---

### Task 4: Extract the grain stage

**Files:**
- Create: `src/pipeline/stage_grain.c`
- Modify: `src/main/main.c`, `CMakeLists.txt`

- [ ] **Step 1: Create `stage_grain.c`** with `StageStatus stage_grain(PipelineCtx *ctx)`: move the `/* ---- Grain analysis ---- */` block verbatim (cache load, cached vs fresh branches, cache save, failure path). Locals map: `film_grain` → `ctx->film_grain`, `grain` → `ctx->grain`, `scores_cached`/`cached_*` → ctx fields, `cli_quality` → `ctx->opt.quality`, `cli_crf`/`cli_bitrate` → `ctx->opt.crf`/`ctx->opt.bitrate`, helpers → `vmav_load_cached_scores`/`vmav_save_cached_scores`. Note the grain-failure path today does **not** return — it prints FAIL + hint and execution continues (grain.error checked downstream). Preserve exactly: the stage returns `STAGE_CONTINUE` even on grain failure.
  Also move the two lines that follow the block: `enc_preset = get_encode_preset(…)` → `ctx->enc_preset = get_encode_preset(ctx->opt.quality, ctx->info.height);` and the 4K-source guard for `--companion-hd`/`--scale-to-hd` (that one *does* return `STAGE_EXIT_FAIL`).
- [ ] **Step 2: Rewire main** (same `st != STAGE_CONTINUE` pattern). **Build + tests. Smoke: dry-run diff vs baseline — empty.**
- [ ] **Step 3: Format + commit** `refactor(main): extract grain-analysis stage`.

---

### Task 5: Extract the naming stage

**Files:**
- Create: `src/pipeline/stage_naming.c`
- Modify: `src/main/main.c`, `CMakeLists.txt`

- [ ] **Step 1: Create `stage_naming.c`** containing, moved verbatim: the three interactive prompts `ask_language_tag`, `ask_source`, `ask_positive_int` (stay `static`, keep names; `parse_int_or_zero` → `vmav_parse_int_or_zero`), and `StageStatus stage_naming(PipelineCtx *ctx)` holding the whole `/* ---- Naming setup (TMDB or --blind) ---- */` chain: blind branch, TMDB movie/TV branch (S/E resolution, `tmdb_fetch_tv`/`tmdb_fetch_episode`/`tmdb_fetch_movie`, source/variant/language-tag resolution, `build_output_filename`, `FrenchAudioOrigin` switch, `mkv_title` construction), and the no-source else branch (added in `edaa792`) → `STAGE_EXIT_FAIL`.
  Mapping: `naming_ok = true` at the end of a successful branch becomes `return STAGE_CONTINUE;`; the TMDB `meta_ok == false` fallthrough (fetch failed: today `naming_ok` stays false and main just… skips the whole encode body and exits 0 at the bottom — **that is the current behavior; preserve it** by returning `STAGE_EXIT_OK` when `!meta_ok` after the fetch-failure UI lines). All naming outputs land in the corresponding `ctx->` fields (`output_name`, `base_name`, `output_dir`, `mkv_title`, `saved_tmdb_*`, `saved_episode`, `saved_is_tv`, `video_language`, `source`, `fv`, `fr_audio_origin`, `resolved_lang_tag`). The early failure returns inside the TV S/E prompts (`season unknown` / `episode unknown`) become `STAGE_EXIT_FAIL`.
  With the stage returning status, the `if (naming_ok) { … }` wrapper around the rest of main is **removed** (unindent the body by one level — this is the one large mechanical reindent of the refactor; do it with the formatter, not by hand-editing content).
- [ ] **Step 2: Rewire main; build + tests. Smoke: dry-run diff vs baseline — empty. Also spot-check the no-naming path:**

```bash
cd "$SCRATCH/muxtest" && "$BIN" --dry-run --cache-dir cache \
  "../muxsrc/Neon Genesis Evangelion - Episode 01.mkv" > out.txt 2>&1; echo "RC=$?"
```

Expected: RC=1 and `[FAIL] Naming` + the `--tmdb/--blind` hint in `out.txt`.
- [ ] **Step 3: Format + commit** `refactor(main): extract naming stage`.

---

### Task 6: Extract the audio stage

**Files:**
- Create: `src/pipeline/stage_audio.c`
- Modify: `src/main/main.c`, `CMakeLists.txt`

- [ ] **Step 1: Create `stage_audio.c`** with `StageStatus stage_audio(PipelineCtx *ctx)`: move the `/* ---- OPUS audio encoding ---- */` block verbatim — `select_best_audio_per_language` (enc_best), the qsort with `vmav_cmp_audio_order`, the per-track loop (VF2 variant detection, `build_opus_filename`, `vmav_build_cache_path`, `encode_track_to_opus`, skip/ok/fail UI). Outputs → `ctx->opus_paths/audio_names/audio_langs/opus_count/audio_fail_count`; a track failure sets `ctx->pipeline_failed = 1` (as today) and the stage still returns `STAGE_CONTINUE` (today execution continues to subtitles and the mux-skip logic handles it). `free(enc_best)` moves **into** this stage (it currently happens at the very bottom of main; freeing right after the loop is safe because nothing else reads it — verify with a grep for `enc_best` before deleting the old free).
- [ ] **Step 2: Rewire, build, tests. Smoke: dry-run diff — empty** (audio block is `!dry_run`-guarded; the section header itself is inside the guard so the transcript is unchanged).
- [ ] **Step 3: Format + commit** `refactor(main): extract audio stage`.

---

### Task 7: Extract the subtitle stage

**Files:**
- Create: `src/pipeline/stage_subs.c`
- Modify: `src/main/main.c`, `CMakeLists.txt`

- [ ] **Step 1: Create `stage_subs.c`** with `StageStatus stage_subs(PipelineCtx *ctx)`: move verbatim the `/* ---- Subtitle processing ---- */` block (text extraction via `VmavCommand`, PGS OCR path, per-variant dedup), the `/* ---- Add user-supplied SRT files ---- */` block, and the `/* ---- Sort subtitles … ---- */` stable-insertion-sort block (`sub_sort_key` → `vmav_sub_sort_key`). Outputs → the `ctx->srt_*` arrays and `ctx->srt_count`. Always returns `STAGE_CONTINUE` (subtitle failures today print FAIL but don't set `pipeline_failed` and don't abort — preserve).
- [ ] **Step 2: Rewire, build, tests. Smoke: dry-run diff — empty.**
- [ ] **Step 3: Format + commit** `refactor(main): extract subtitle stage`.

---

### Task 8: Create the unified encode pass — wire the 4K path

**Files:**
- Create: `src/pipeline/encode_pass.c`
- Modify: `src/main/main.c`, `CMakeLists.txt`

This is the load-bearing task. It creates the parameterized pass and switches the **4K path only**; the HD block stays inline and untouched until Task 9.

- [ ] **Step 1: `StageStatus stage_rate_plan(PipelineCtx *ctx, EncodePassParams *pass)`** — from the `/* ---- Rate control: CRF search or manual override ---- */` + `/* Plan + Dry-run notice … */` blocks:
  - Rate control: if `!opt.grain_only && pass->crf == 0 && opt.bitrate == 0`: when `pass->use_cached_crf && ctx->scores_cached && ctx->cached_crf > 0` take the cached CRF (with the existing "using cached CRF %d" UI); otherwise `run_crf_search(opt.filepath, vmaf, pass->preset, pass->film_grain, pass->crf_scale_filter)` — failure prints the existing FAIL+hint and returns `STAGE_EXIT_FAIL`. `vmaf` = `opt.vmaf_target > 0 ? opt.vmaf_target : get_vmaf_target(opt.quality, pass->plan_info->height)` — **note:** the 4K path today calls `get_vmaf_target(cli_quality, info.height)` and the HD path precomputes `hd_vmaf_default` from `hd_h`; passing `plan_info->height` reproduces both. Else-branch `cli_vmaf_target > 0 → vmaf_used = cli_vmaf_target` as today.
  - Plan section: quiet-bracketing (`ui_set_quiet(0)` … restore), section title `pass->is_hd && ctx->opt.companion_hd ? "HD encoding plan" : "Encoding plan"`; Preset kv — 4K variant prints `(info.height >= 2160 ? "4K" : "HD")`, HD variant prints literal `"(HD)"` plus the extra `Scale` kv (`hd_w×hd_h from info.width×info.height`); reproduce with `if (pass->is_hd) { … "(HD)" + Scale kv … } else { … }` — do **not** try to over-unify the two format strings. Grain tier kv, CRF/Bitrate kv, Output kv (`ctx->output_dir` + `pass->output_name`), `vmav_print_encoder_knobs` when `opt.grain_only`.
  - Dry-run exit: `if ((opt.dry_run || opt.grain_only) && !pass->dry_run_falls_through)` → print the existing "Dry run / Grain-only" section and return `STAGE_EXIT_OK`. (For the 4K pass `dry_run_falls_through = opt.companion_hd`, matching today's `&& !companion_hd`. For the HD pass it's `false` — it always exits.)
- [ ] **Step 2: `StageStatus encode_pass(PipelineCtx *ctx, EncodePassParams *pass)`** — from the RPU + video + mux + cleanup + Done blocks:
  - RPU: only when `pass->extract_rpu && ctx->hdr.error == 0 && ctx->hdr.has_dolby_vision`; filename from `pass->base_name`; result path into `ctx->rpu_path`; failure clears `ctx->rpu_path[0]` and continues (as today).
  - Video encode: `VideoEncodeConfig` exactly as the current 4K block, plus `.scale_width/.scale_height = pass->scale_width/height` (0/0 for 4K — **verify** `VideoEncodeConfig` treats 0 as "no scaling"; the current 4K initializer simply omits the fields, and C zero-initializes the rest of a designated initializer, so 0/0 is already what the 4K path passes today), `.hdr = pass->enc_hdr`, `.crf = pass->crf`, `.preset = pass->preset`, `.film_grain = pass->film_grain`. Section label `pass->is_hd && ctx->opt.companion_hd ? "HD video encoding" : "Video encoding"`. Failure sets `ctx->pipeline_failed = 1` and continues to the mux-skip logic (as today).
  - Mux: build `MuxAudioTrack`/`MuxSubtitleTrack` arrays from ctx (identical loop, including the first-French-forced default rule), `FinalMuxConfig` with `pass->output_name` appended to `ctx->output_dir`; the `audio_fail_count > 0` / video-failed skip branches; success/skip/fail UI — all exactly as the current code (the 4K and HD versions of this block are already token-identical apart from variable names).
  - Cleanup on mux success: remove opus+srt intermediates **unless** `pass->defer_shared_cleanup`; always remove the pass's video.mkv; remove `ctx->rpu_path` unless `pass->defer_shared_cleanup` (4K/companion case — matches today's `!companion_hd` guards); `vmav_cleanup_cache_dir(ctx)` unless `pass->defer_shared_cleanup`. (HD pass: `defer_shared_cleanup = false` → removes shared intermediates + rpu + cache dir, matching today's HD cleanup.)
  - Done receipt: only when mux ok and not skipped; the bitrate-delta line only when `!pass->is_hd && opt.bitrate > 0 && avg_kbps > 0` (today the HD receipt has no bitrate line); section label `pass->is_hd && ctx->opt.companion_hd ? "HD Done" : "Done"`.
- [ ] **Step 3: Wire the 4K path in main:**

```c
EncodePassParams pass4k = {
    .preset = ctx->enc_preset,
    .film_grain = ctx->film_grain,
    .crf = ctx->opt.crf,
    .base_name = ctx->base_name,
    .output_name = ctx->output_name,
    .crf_scale_filter = NULL,
    .plan_info = &ctx->info,
    .enc_hdr = &ctx->hdr,
    .is_hd = false,
    .use_cached_crf = true,
    .dry_run_falls_through = ctx->opt.companion_hd,
    .extract_rpu = true,
    .defer_shared_cleanup = ctx->opt.companion_hd,
};
if (!ctx->opt.scale_to_hd) {
  st = stage_rate_plan(ctx, &pass4k);
  if (st != STAGE_CONTINUE) { rc = (st == STAGE_EXIT_FAIL); goto out; }
}
st = stage_audio(ctx);   /* internally no-ops on dry_run/grain_only */
st = stage_subs(ctx);
if (!ctx->opt.scale_to_hd && !ctx->opt.dry_run && !ctx->opt.grain_only) {
  st = encode_pass(ctx, &pass4k);
  if (st != STAGE_CONTINUE) { rc = (st == STAGE_EXIT_FAIL); goto out; }
}
/* HD block: still the original inline code, now reading ctx-> fields. */
```

  **Ordering note:** this preserves today's sequence exactly — 4K rate+plan happens *before* audio/subs, the 4K encode/mux after. Delete the now-moved 4K blocks from main.
- [ ] **Step 4: Build + tests.** Smoke battery (all against baseline):
  - dry-run diff — empty;
  - grain-only diff vs `grainonly.txt` — empty;
  - **full encode+mux run** (Task 0 Step 3 command, output to a fresh log): expect RC=0; then layout + stream-hash comparison against `$SCRATCH/thinmain-baseline/` per the determinism verdict in `NOTES.txt` (deterministic streams md5-equal; video stream packet-count + keyframe-count equal). Restore/remove the produced output afterward so later tasks start clean.
- [ ] **Step 5: Format + commit** `refactor(main): unified encode pass, 4K path wired`.

---

### Task 9: Switch the HD block to the unified pass

**Files:**
- Modify: `src/main/main.c` (delete the ~320-line inline HD block), `src/pipeline/encode_pass.c` (only if a param proves missing — flag it, don't improvise), `CMakeLists.txt` (no change expected)

- [ ] **Step 1: In main, replace the whole `if (do_hd) { … }` block with:**

```c
if (do_hd) {
  /* HD output dimensions from the cropped source AR (width 1920,
     height even, min 2) — moved verbatim from the old HD block. */
  int hd_crop_w = ctx->info.width - (ctx->crop.error == 0 ? ctx->crop.left + ctx->crop.right : 0);
  int hd_crop_h = ctx->info.height - (ctx->crop.error == 0 ? ctx->crop.top + ctx->crop.bottom : 0);
  hd_crop_w &= ~1;
  hd_crop_h &= ~1;
  int hd_w = 1920;
  int hd_h = (int)((double)hd_crop_h * hd_w / hd_crop_w) & ~1;
  if (hd_h < 2)
    hd_h = 2;

  MediaInfo hd_info = ctx->info;
  hd_info.width = hd_w;
  hd_info.height = hd_h;
  HdrInfo hd_hdr = ctx->hdr; /* DV Profile 8.1 is resolution-independent */

  char hd_output_name[1024] = "";
  char hd_base_name[1024] = "";
  if (ctx->saved_tmdb_title[0]) {
    build_output_filename(hd_output_name, sizeof(hd_output_name), ctx->saved_tmdb_title,
                          ctx->saved_tmdb_year, ctx->resolved_lang_tag, &hd_info, &hd_hdr,
                          ctx->source, ctx->saved_is_tv ? &ctx->saved_episode : NULL);
    snprintf(hd_base_name, sizeof(hd_base_name), "%s", hd_output_name);
    char *hd_ext = strrchr(hd_base_name, '.');
    if (hd_ext && strcmp(hd_ext, ".mkv") == 0)
      *hd_ext = '\0';
  } else {
    snprintf(hd_base_name, sizeof(hd_base_name), "%s-HDLight", ctx->base_name);
    snprintf(hd_output_name, sizeof(hd_output_name), "%s.mkv", hd_base_name);
  }

  EncodePassParams passhd = {
      .preset = get_encode_preset(ctx->opt.quality, hd_h),
      .film_grain = get_film_grain_from_score(
          ctx->grain.error == 0 ? ctx->grain.grain_score : 0.0,
          ctx->grain.error == 0 ? ctx->grain.grain_variance : 0.0, ctx->opt.quality),
      .crf = ctx->opt.crf,
      .base_name = hd_base_name,
      .output_name = hd_output_name,
      .scale_width = hd_w,
      .scale_height = hd_h,
      .crf_scale_filter = "scale=1920:1080:flags=lanczos",
      .plan_info = &hd_info,
      .enc_hdr = &hd_hdr,
      .is_hd = true,
      .use_cached_crf = false,
      .dry_run_falls_through = false,
      .extract_rpu = ctx->opt.scale_to_hd,
      .defer_shared_cleanup = false,
  };
  st = stage_rate_plan(ctx, &passhd);
  if (st != STAGE_CONTINUE) { rc = (st == STAGE_EXIT_FAIL); goto out; }
  st = encode_pass(ctx, &passhd);
  if (st != STAGE_CONTINUE) { rc = (st == STAGE_EXIT_FAIL); goto out; }
}
```

  **Behavior-difference audit before deleting the old block — reconcile each against `encode_pass`:** (a) the HD plan's `Scale` kv prints `hd_w×hd_h (from info.width×info.height 4K source)` — ensure `stage_rate_plan` renders it for `is_hd` using `plan_info` vs `ctx->info`; (b) HD `VideoEncodeConfig` passes `.info = &info` (source dims, NOT hd_info) with `.scale_width/height` set — `encode_pass` must always pass `.info = &ctx->info`; (c) the HD Done receipt uses `hd_info.duration` — equal to `ctx->info.duration` by construction (only width/height are overwritten), so `encode_pass` using `ctx->info.duration` is faithful; (d) HD RPU filename derives from `hd_base_name` — covered by `pass->base_name`; (e) the HD mux failure/skip branches and cleanup are the ones `encode_pass` already implements with `defer_shared_cleanup=false`. If any other divergence turns up during the diff-read of the old block, STOP and report it rather than silently absorbing it.
- [ ] **Step 2: Build + tests.** Smoke battery:
  - dry-run + grain-only diffs vs baseline — empty;
  - `--companion-hd --dry-run` run: expect the 4K plan **and** the HD plan sections both render (fall-through), RC=0, and the process exits at the HD dry-run notice;
  - `--scale-to-hd --dry-run` run: expect only the HD plan, RC=0.
  (The 60 s clip is 1080p — `--companion-hd`/`--scale-to-hd` require a 4K source and will fail the height guard. Use `V_clip.mkv` in the repo root — the 4K HDR test clip — for these two dry-run checks, with a scratch cache dir.)
- [ ] **Step 3: Format + commit** `refactor(main): HD pass uses the unified encode pass`.

---

### Task 10: Final slim-down + full validation

**Files:**
- Modify: `src/main/main.c`

- [ ] **Step 1:** `main()` should now read as: banner → prescan → config/deps → parse → cache-dir setup → `stage_probe` → `stage_grain` → `stage_naming` → `stage_rate_plan(4K)` → `stage_audio` → `stage_subs` → `encode_pass(4K)` → HD block → teardown. Remove any now-dead locals/includes from main.c (compile with the existing warning flags; the build must be warning-clean like before). Target: main.c < 250 lines. Do NOT chase the number by compressing readable code — if it lands at 300, fine.
- [ ] **Step 2: Full validation battery:**
  1. `cmake --build build && ./build/vmav_tests` — pass.
  2. Dry-run + grain-only transcripts vs baseline — byte-identical.
  3. Full encode+mux on the 60 s clip (background), RC=0, layout/stream comparison vs baseline per `NOTES.txt`.
  4. Playability spot-check on the produced file: `ffprobe` streams + chapters + keyframe count, and confirm the container title / track names / dispositions match `layout.json`.
  5. No-naming failure path: RC=1 with `[FAIL] Naming`.
- [ ] **Step 3: Format** (`uvx --from clang-format==18.1.8 clang-format -i src/main/main.c src/pipeline/*.c include/vmavificient/pipeline.h`), rebuild, retest.
- [ ] **Step 4: Commit** `refactor(main): thin driver — pipeline stages fully extracted`.

---

## Self-review notes

- **Spec coverage:** thin main (Tasks 1–10), duplicate-block unification (8–9), zero behavior change (baseline in 0, diffs in every task), module conventions (headers in `include/vmavificient/`, sources per-dir, CMake registration each task).
- **Known intentional non-goals:** no bug fixes (the `best` leak, the chroma-score approximation stay); no new unit tests (stages are ffmpeg-driven; the regression oracle is the transcript/stream baseline); no CLI changes.
- **Type consistency:** all stage signatures and struct fields are defined once in Task 1's `pipeline.h` and referenced verbatim afterward.
