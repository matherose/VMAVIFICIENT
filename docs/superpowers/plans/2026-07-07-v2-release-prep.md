# v2.0.0 Release Preparation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refresh the entire vendored dependency stack to latest stable, fix the two known silent-failure traps (SVT log suppression, missing tessdata), replace all `system()` shell-outs with posix_spawn, harden CI (unit tests actually run, tag↔version guard, checkout@v5, media cache), execute the thin-main refactor, add a clang-tidy CI gate, and ship v2.0.0.

**Owner-approved scope (2026-07-07):** ALL optional items are IN — `system()`→posix_spawn (Task 4), thin-main refactor (Phase D, own plan doc), clang-tidy CI gate (Task 7, after the refactor so findings reflect final code).

**Architecture:** Dependency bumps are CMakeLists one-liners validated by a full vendored static build (the only build path that exercises them — the system-deps path uses Homebrew). Code fixes are small, isolated C changes on the fast system-deps build. CI changes are pure YAML. Release last, gated on a green `workflow_dispatch` static build.

**Tech Stack:** CMake ExternalProject, FFmpeg 8, SVT-AV1-HDR, Tesseract/Leptonica, libvmaf, GitHub Actions.

**MAJOR-bump justification:** FFmpeg 8+ is now a hard floor (v1.6.0 commit 68eb31b), SVT-AV1 diagnostics become visible by default (output behavior change), missing tessdata becomes a hard early failure instead of a silent sub-less encode, and the whole vendored stack moves including OpenSSL 3.3→3.5 LTS.

**Standing rules (bind every subagent):**
- Commit trailer: `Assisted-by: Claude Fable 5 <noreply@anthropic.com>` — never Co-Authored-By.
- `git add` explicit paths only. NEVER `git add -A` (huge untracked media in repo root).
- Format only with `uvx --from clang-format==18.1.8 clang-format` — never the local v22.
- ALL vmavificient pipeline runs go in background (grain analysis ≈ 8 min).
- TMDB API key lives in config.ini — never print or commit it.
- Build: `cmake --build build` · Tests: `./build/vmav_tests`.

---

## Dependency survey (2026-07-07, via `git ls-remote --sort=-v:refname`)

| Dependency | Pinned | Latest stable | Action |
|---|---|---|---|
| FFmpeg | n8.1 | **n8.1.2** | bump (user-requested) |
| opus | v1.5.2 | **v1.6.1** | bump |
| cJSON | v1.7.18 | **v1.7.19** | bump |
| zlib | v1.3.1 | **v1.3.2** | bump |
| OpenSSL | openssl-3.3.2 | **openssl-3.5.7** (LTS→2030); 3.6.3/4.0.1 exist | bump to LTS line |
| curl | curl-8_11_0 | **curl-8_21_0** | bump |
| libpng | v1.6.43 | **v1.6.58** | bump (many CVE fixes since .43) |
| libjpeg-turbo | 3.1.0 | **3.2.0** | bump |
| libtiff | v4.7.0 | **v4.7.2** | bump |
| leptonica | 1.85.0 | **1.86.0** | bump (pair with tesseract) |
| tesseract | 5.5.0 | **5.5.2** | bump (old 5.5.2 gotchas were zig-musl/mingw cross-toolchain; current builds are native macOS — expect clean, but validate) |
| libvmaf | v3.1.0 | **v3.2.0** | bump (in-process CRF search links it — re-run oracle) |
| SVT-AV1-HDR | v4.1.0 | v4.1.0 | current ✓ |
| libdovi | libdovi-3.3.2 | libdovi-3.3.2 | current ✓ |
| libhdr10plus | libhdr10plus-2.1.5 | libhdr10plus-2.1.5 | current ✓ |

**Risk notes:** OpenSSL 3.3→3.5 + curl 8.11→8.21 build together (curl links vendored OpenSSL) — if curl's configure options changed, the vendored build fails loudly, fix flags then. libvmaf 3.2 may shift VMAF scores slightly — oracle acceptance below tolerates it. Opus 1.6 changes encoder internals — output size differences on re-encode are expected and fine.

---

### Task 1: Bump all vendored dependency pins + launch vendored validation build

**Files:**
- Modify: `CMakeLists.txt:388-401`

- [ ] **Step 1: Apply the ten pin edits** (exact old → new, one Edit each):

```cmake
set(FFMPEG_GIT_TAG    "n8.1")        →  set(FFMPEG_GIT_TAG    "n8.1.2")
set(OPUS_GIT_TAG      "v1.5.2")      →  set(OPUS_GIT_TAG      "v1.6.1")
set(CJSON_GIT_TAG     "v1.7.18")     →  set(CJSON_GIT_TAG     "v1.7.19")
set(ZLIB_GIT_TAG      "v1.3.1")      →  set(ZLIB_GIT_TAG      "v1.3.2")
set(OPENSSL_GIT_TAG   "openssl-3.3.2") →  set(OPENSSL_GIT_TAG   "openssl-3.5.7")
set(CURL_GIT_TAG      "curl-8_11_0") →  set(CURL_GIT_TAG      "curl-8_21_0")
set(LIBPNG_GIT_TAG    "v1.6.43")     →  set(LIBPNG_GIT_TAG    "v1.6.58")
set(LIBJPEG_GIT_TAG   "3.1.0")       →  set(LIBJPEG_GIT_TAG   "3.2.0")
set(LIBTIFF_GIT_TAG   "v4.7.0")      →  set(LIBTIFF_GIT_TAG   "v4.7.2")
set(LEPTONICA_GIT_TAG "1.85.0")      →  set(LEPTONICA_GIT_TAG "1.86.0")
set(TESSERACT_GIT_TAG "5.5.0")       →  set(TESSERACT_GIT_TAG "5.5.2")
set(LIBVMAF_GIT_TAG   "v3.1.0")      →  set(LIBVMAF_GIT_TAG   "v3.2.0")
```

Leave `SVTAV1_GIT_TAG`, `LIBDOVI_GIT_TAG`, `LIBHDR10PLUS_GIT_TAG` untouched (already latest). Also update `LIBHDR10PLUS_VERSION`/`LIBDOVI_VERSION` only if their tags change (they don't).

- [ ] **Step 2: Launch the vendored static build in background** (this is the validation path; it takes 30–90 min):

```bash
cmake -G Ninja -B build-vendored -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CROSSCOMPILING=ON -DVMAV_USE_SYSTEM_DEPS=OFF \
  && cmake --build build-vendored -j"$(sysctl -n hw.ncpu)"
```

Run in background (timeout 600000 per call; re-poll). Expected: configures clean, builds all ExternalProjects, links `build-vendored/vmavificient`.

- [ ] **Step 3: Verify the system-deps build still configures** (floors unaffected, but cheap):

Run: `cmake --build build 2>&1 | tail -2` — Expected: no re-run needed or clean rebuild.

**Do NOT commit yet** — the commit lands in Task 4 after vendored validation passes.

---

### Task 2: SVT-AV1 diagnostics visible by default

**Files:**
- Modify: `src/video_encode/video_encode.c:46-53`

The current callback early-returns on `!ui_is_verbose()` before any level check, swallowing even FATAL — this masked the v1.5.0 single-keyframe defect for a month. The comment already claims "We always forward warn+ to stderr"; make the code match the comment.

- [ ] **Step 1: Change the gate** (keep the rest of the function body — the switch and vfprintf — untouched):

```c
static void svt_log_callback(void *context, SvtAv1LogLevel level, const char *tag, const char *fmt,
                             va_list args) {
  (void)context;
  /* SVT-AV1 levels: 0=fatal, 1=error, 2=warn, 3=info, 4=debug. Fatal,
     error, and warn always reach stderr — suppressing them behind
     --verbose masked a FATAL misconfiguration for a month (the v1.5.0
     single-keyframe defect). Info and debug stay verbose-gated. */
  if (level > SVT_AV1_LOG_WARN && !ui_is_verbose())
    return;
```

- [ ] **Step 2: Build + tests**

Run: `cmake --build build && ./build/vmav_tests` — Expected: builds clean, "All tests passed".

- [ ] **Step 3: Verify no info-spam regression**

Run a short blind encode WITHOUT `--verbose` in background (use the 5s BBB sample or any small clip); confirm SVT info lines (config banners) do NOT appear, and the run completes exit 0.

- [ ] **Step 4: Commit**

```bash
git add src/video_encode/video_encode.c
git commit -m "fix(video_encode): always surface SVT-AV1 fatal/error/warn diagnostics"
```

---

### Task 3: OCR preflight — fail fast when tessdata is missing

**Files:**
- Modify: `src/subtitle_convert/subtitle_convert.c` (extract resolver from lines 573-590)
- Modify: `include/vmavificient/subtitle_convert.h`
- Modify: `src/main/main.c` (insert preflight before the grain-analysis block, ~line 1260 — anchor: the `ui_stage_ok("Grain analysis", "loaded from cache")` cluster)

Today a missing tessdata dir is discovered only after ~8 min of grain analysis, and the encode continues sub-less. Preflight at startup instead.

- [ ] **Step 1: Extract and expose the resolver.** In `subtitle_convert.c`, lift the TESSDATA_PREFIX/common-paths lookup (lines 575-590) into:

```c
int subtitle_ocr_preflight(const char *tesseract_lang, char *out_dir, size_t out_size) {
  const char *tessdata_path = getenv("TESSDATA_PREFIX");
  if (!tessdata_path || !tessdata_path[0]) {
    static const char *paths[] = {
        "/usr/local/share/tessdata",
        "/opt/homebrew/share/tessdata",
        NULL,
    };
    tessdata_path = NULL;
    for (int i = 0; paths[i]; i++) {
      struct stat tst;
      if (stat(paths[i], &tst) == 0) {
        tessdata_path = paths[i];
        break;
      }
    }
  }
  if (!tessdata_path)
    return -1;
  char model[1024];
  snprintf(model, sizeof(model), "%s/%s.traineddata", tessdata_path, tesseract_lang);
  struct stat mst;
  if (stat(model, &mst) != 0)
    return -1;
  if (out_dir)
    snprintf(out_dir, out_size, "%s", tessdata_path);
  return 0;
}
```

Declare it in `subtitle_convert.h` with a brief comment (`0 = usable tessdata found, -1 = missing dir or <lang>.traineddata`). Refactor `convert_pgs_to_srt` to call it (keeping its existing error path for the init failure).

- [ ] **Step 2: Wire the preflight into main.c.** Before the grain stage, for each PGS subtitle track selected for OCR (reuse the same selection predicate the OCR loop at ~line 1782 uses — language match + PGS codec), call `subtitle_ocr_preflight(iso639_to_tesseract_lang(track->language), NULL, 0)`. On failure:

```c
ui_stage_fail("OCR preflight", "no usable tessdata for language");
ui_hint("install tessdata_best (eng+fra) and set TESSDATA_PREFIX, or drop PGS tracks");
return 1;
```

- [ ] **Step 3: Build + tests** — `cmake --build build && ./build/vmav_tests` → "All tests passed".

- [ ] **Step 4: Manual validation (both directions)**

  - `TESSDATA_PREFIX=/nonexistent ./build/vmavificient …` on the TV fixture clip (background): expect `[FAIL] OCR preflight` within seconds, exit 1, NO grain analysis started.
  - Normal env warm-cache re-run on the fixture: expect exit 0, SRT track present in output (assert with ffprobe).

- [ ] **Step 5: Commit**

```bash
git add src/subtitle_convert/subtitle_convert.c include/vmavificient/subtitle_convert.h src/main/main.c
git commit -m "feat(subtitle_convert): preflight tessdata before pipeline start"
```

---

### Task 4: Replace all `system()` shell-outs with posix_spawn

**Files:**
- Create: `src/utils/proc.c`, `include/vmavificient/proc.h`
- Modify: `src/final_mux/final_mux.c` (site: line ~177 — ffmpeg mux command), `src/main/main.c` (sites: ~363 background cache rm, ~378 in-place cache rm, ~1806 ffmpeg subtitle extract)
- Modify: `CMakeLists.txt` (add `src/utils/proc.c` to the executable + vmav_tests source lists as the existing entries do)

Shell-string building is fragile for the Japanese/quoted/spaced filenames this tool encodes daily, and both cache-rm sites have real quoting bugs today (double-quoting via shell_quote_append + `'%s'`; backslash-escaping inside single quotes which sh ignores). cert-env33-c also bans `system()`.

- [ ] **Step 1: Write the helpers** in `src/utils/proc.c`:

```c
#include "vmavificient/proc.h"

#include <errno.h>
#include <ftw.h>
#include <posix_spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int vmav_run(char *const argv[]) {
  pid_t pid;
  int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
  if (rc != 0) {
    fprintf(stderr, "  Error: cannot spawn '%s': %s\n", argv[0], strerror(rc));
    return -1;
  }
  int status;
  if (waitpid(pid, &status, 0) < 0)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int rm_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)st;
  (void)flag;
  (void)ftw;
  return remove(path);
}

int vmav_rmtree(const char *path) {
  return nftw(path, rm_cb, 32, FTW_DEPTH | FTW_PHYS);
}

int vmav_rmtree_async(const char *path) {
  /* Double-fork so the grandchild is reparented to init and we never
     leave a zombie nor disturb any future SIGCHLD handling. */
  pid_t pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    pid_t inner = fork();
    if (inner == 0) {
      (void)vmav_rmtree(path);
      _exit(0);
    }
    _exit(inner < 0 ? 1 : 0);
  }
  int status;
  (void)waitpid(pid, &status, 0);
  return 0;
}
```

`proc.h`: include guards, brief file comment, the three prototypes. NOTE: `nftw` needs `#define _XOPEN_SOURCE 700` BEFORE all includes if the build is strict `-std=c11` with extensions off — check `CMAKE_C_EXTENSIONS` in CMakeLists and add the define at the top of proc.c if needed.

**Command builder for the ffmpeg sites:** an argv array builder — fixed arena, no heap:

```c
typedef struct {
  char buf[16384];   /* argument string storage */
  size_t pos;
  char *argv[256];   /* NULL-terminated argv */
  int argc;
  int overflow;
} VmavCommand;

void vmav_cmd_init(VmavCommand *c);
void vmav_cmd_arg(VmavCommand *c, const char *arg);            /* verbatim copy */
void vmav_cmd_argf(VmavCommand *c, const char *fmt, ...);      /* printf-style */
```

Each arg is copied into `buf`, pointer stored in `argv`; on overflow set `c->overflow = 1` (call sites must check before vmav_run). Implement in proc.c, declare in proc.h.

- [ ] **Step 2: Convert `final_mux.c`.** Replace the shell-string builder (`cmd_appendf` + `shell_quote_append` + `system`) with VmavCommand: every existing `cmd_appendf(cmd, cap, &pos, " -flag value")` fragment becomes discrete `vmav_cmd_arg` calls (`"-flag"`, `"value"`); every `shell_quote_append(..., path)` becomes `vmav_cmd_arg(c, path)` — no quoting at all. Keep the exact ffmpeg argument order. Exit-code handling: `int exit_code = vmav_run(c.argv);` replaces the system/WIFEXITED block. Delete `shell_quote_append`/`cmd_appendf` from final_mux.c if now unused there.

- [ ] **Step 3: Convert the two cache-rm sites in main.c** (~363 and ~378): background one → `vmav_rmtree_async(old_cache_path)`; in-place one → `if (vmav_rmtree(g_cache_dir) != 0) { …existing warning… }`. Delete the escaping scaffolding.

- [ ] **Step 4: Convert the subtitle-extract site** (main.c ~1806): same VmavCommand treatment as final_mux.

- [ ] **Step 5: Verify zero `system()` remains:**

Run: `grep -rn "system(" src/ --include='*.c' | grep -v "_system\|system deps"` — Expected: no matches.

- [ ] **Step 6: Build + tests + real-content validation.** `cmake --build build && ./build/vmav_tests`, then a warm-cache fixture run in background — the mux and SRT-extract paths MUST produce the same ffprobe track layout as before (this exercises both converted ffmpeg call sites).

- [ ] **Step 7: Commit**

```bash
git add src/utils/proc.c include/vmavificient/proc.h src/final_mux/final_mux.c src/main/main.c CMakeLists.txt
git commit -m "refactor(proc): replace system() with posix_spawn/nftw helpers"
```

---

### Task 5: Vendored build validation + dependency commit

**Depends on Task 1's background build finishing.**

- [ ] **Step 1: Confirm vendored build success** — link completed, `build-vendored/vmavificient` exists; on failure, fix the failing dep's configure flags (most likely curl↔OpenSSL) and rebuild before proceeding.

- [ ] **Step 2: Run the vendored binary's checks**

```bash
./build-vendored/vmav_tests            # All tests passed
./build-vendored/vmavificient 2>&1 | head -1   # banner shows SVT-AV1-HDR v4.1.0
```

- [ ] **Step 3: CRF-search oracle** (libvmaf 3.2 + FFmpeg 8.1.2 in-process path). Re-run the CRF search on the fixture clip with the same grain inputs, in background. Acceptance: exit 0 and chosen CRF within ±2 of 35 (VMAF near 94). A small score drift from libvmaf 3.2 is acceptable; a failure or wild CRF is not.

- [ ] **Step 4: Commit the bumps**

```bash
git add CMakeLists.txt
git commit -m "build(deps): refresh vendored stack (FFmpeg n8.1.2, OpenSSL 3.5 LTS, tesseract 5.5.2, libvmaf 3.2, +8 more)"
```

---

### Task 5b: Vendor dav1d — fix vendored-build CRF scoring (found by the oracle)

**Discovery 2026-07-07:** the CRF oracle FAILS on the vendored binary — `score_sample failed at sample 0 (480 packets, scored 0)`. Root cause: `crf_search.c:605` needs `avcodec_find_decoder(AV_CODEC_ID_AV1)` to software-decode the probe encodes, but the vendored FFmpeg (`--disable-autodetect`, no external AV1 decoder) only has the native `av1` decoder, which is hwaccel-oriented and yields no frames here. Homebrew FFmpeg ships dav1d, so the system build passes. **Pre-existing defect, not a bump regression: every static release binary since #21 has a broken CRF search** (CI never caught it because the vendored jobs run `--bitrate`, which skips CRF search).

**Files:** Modify `CMakeLists.txt` (vendored branch).

- [ ] Add `set(DAV1D_GIT_TAG "1.5.3")` beside the other pins and a `DAV1D_INSTALL` prefix beside the others.
- [ ] Add `ep_dav1d` ExternalProject cloning `https://code.videolan.org/videolan/dav1d.git`, meson pattern copied from `ep_libvmaf` (`--default-library=static`, `-Denable_tools=false -Denable_tests=false`), byproduct `libdav1d.a`.
- [ ] `ep_ffmpeg`: add `ep_dav1d` to DEPENDS, dav1d's pkgconfig dir to PKG_CONFIG_PATH, and `--enable-libdav1d` to configure.
- [ ] Final vendored link list: add `"${DAV1D_INSTALL}/lib/libdav1d.a"` next to the opus static lib.
- [ ] Rebuild vendored, re-run the CRF oracle: MUST now report a CRF near 35 and exit 0.
- [ ] Commit: `build(deps): vendor dav1d so the static binary can score CRF probes`.

---

### Task 6: CI hardening

**Files:**
- Modify: `.github/workflows/build.yml`
- Delete: `.github/workflows/ci.yml` (its only job duplicates build.yml's format-check; the `develop` branch it also watches doesn't exist)
- Modify: `.pre-commit-config.yaml`

- [ ] **Step 1: Run unit tests in CI.** In `build-macos-system` after the Build step:

```yaml
      - name: Run unit tests
        run: ./build/vmav_tests
```

In `sanitizer` after its Build step (ASAN-instrumented tests):

```yaml
      - name: Run unit tests (sanitizer)
        env:
          ASAN_OPTIONS: halt_on_error=1:detect_leaks=0
          UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1
        run: ./build-asan/vmav_tests
```

- [ ] **Step 2: `actions/checkout@v4` → `@v5`** everywhere in build.yml (4 occurrences) — kills the Node 20 deprecation annotation.

- [ ] **Step 3: Cache the BBB test media** in both `encode-test` and `sanitizer` (before the download step):

```yaml
      - name: Cache test media
        id: bbb-cache
        uses: actions/cache@v4
        with:
          path: bbb.mp4.zip
          key: bbb-sunflower-1080p-30fps
```

and guard the curl with `if: steps.bbb-cache.outputs.cache-hit != 'true'` (keep the unzip + ffmpeg trim unconditional).

- [ ] **Step 4: Tag↔version guard in the release job** (would have caught the banner stuck at 1.2.0 for three releases):

```yaml
      - uses: actions/checkout@v5
      - name: Verify tag matches project version
        run: |
          V=$(sed -n 's/^project(vmavificient VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
          if [ "v$V" != "$GITHUB_REF_NAME" ]; then
            echo "::error::tag $GITHUB_REF_NAME does not match project VERSION $V"
            exit 1
          fi
```

- [ ] **Step 5: Delete `.github/workflows/ci.yml`** (`git rm .github/workflows/ci.yml`).

- [ ] **Step 6: Pin pre-commit clang-format** to the CI version in `.pre-commit-config.yaml`:

```yaml
        entry: bash -c 'uvx --from clang-format==18.1.8 clang-format --dry-run --Werror -style=file "$@"' --
```

Also update the header comment that cites the deleted AGENTS.md (reference `.clang-tidy`/CI instead).

- [ ] **Step 7: Commit**

```bash
git add .github/workflows/build.yml .pre-commit-config.yaml
git rm .github/workflows/ci.yml
git commit -m "ci: run unit tests, guard tag/version match, checkout@v5, cache media, drop redundant workflow"
```

---

### Task 7: Thin-main refactor (own plan document)

**Owner-approved for v2.0.0.** `main()` is ~1,970 lines (main.c:557 onward) orchestrating the whole pipeline inline. Target: main.c becomes init → parse → `pipeline_run()` → report; per-stage functions live in a new `src/pipeline/` module (+ `src/cli/` for options/prompts if natural).

This is too large for inline tasks here. Execution:

- [ ] **Step 1: Write a dedicated plan** (`docs/superpowers/plans/2026-07-07-thin-main-refactor.md`) after a fresh full read of main.c. The lost 2026-06 refactor's decomposition is the map (see memory `project-refactor-modular-architecture`): options parse → cache handle → stage_grain → stage_audio → stage_subtitles → mux-track builders → `render_variant()` collapsing the 4K/HD clone → thin main. The code has moved since (in-process CRF search #21, TV support, pipeline failure gating) — re-derive, don't transplant.
- [ ] **Step 2: Oracle baseline BEFORE any refactor commit.** Encoder is bit-deterministic: capture per-elementary-stream md5s + full ffprobe layout of a warm-cache fixture run (NOT container bytes — Matroska segment-UID is random). Every refactor step must reproduce it exactly.
- [ ] **Step 3: Execute via subagent-driven development**, one extraction per task, oracle re-check + commit per task.
- [ ] **Step 4: Final validation:** fixture run + `TESSDATA_PREFIX=/nonexistent` fail-fast check + `--blind --dry-run` movie regression + vmav_tests.

---

### Task 8: clang-tidy full sweep + CI gate

Runs AFTER the thin-main refactor so findings reflect the final code shape. The zero-findings policy is currently enforced nowhere in CI (only pre-commit, changed files only).

- [ ] **Step 1:** `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (reconfigure in place), then run `run-clang-tidy -p build -quiet` over `src/` in background, capture to scratchpad.
- [ ] **Step 2:** Triage: fix mechanical findings (unchecked returns, const, dead stores) in one commit; anything structural gets fixed only if small, else documented in the final report.
- [ ] **Step 3: Add the CI gate** (owner-approved) once the tree is clean — new job in build.yml:

```yaml
  clang-tidy:
    runs-on: macos-14
    timeout-minutes: 60
    needs: format-check
    steps:
      - uses: actions/checkout@v5
      - name: Install deps
        run: |
          brew install ninja pkg-config nasm meson llvm \
            ffmpeg opus dovi_tool tesseract leptonica libvmaf \
            jpeg-turbo libpng libtiff cjson openssl@3
      - name: Configure (system deps, compile commands)
        run: |
          cmake -G Ninja -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CROSSCOMPILING=ON \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
            -DVMAV_GRAV1SYNTH_BIN_RUNTIME=grav1synth
      - name: Run clang-tidy
        run: |
          "$(brew --prefix llvm)/bin/run-clang-tidy" -p build -quiet \
            $(find src -name '*.c')
```

NOTE: generated headers may require building deps first — if tidy can't resolve includes without a full build, insert a `cmake --build build` step before the tidy run (accept the cost; cache is shared with build-macos-system via the same key family only if paths match — otherwise give it its own `cmake-deps-tidy-macos-arm64-` cache).

- [ ] **Step 4:** Build + tests green; commit fixes + workflow together:

```bash
git add <explicit changed files> .github/workflows/build.yml
git commit -m "ci: clang-tidy gate + resolve full-tree findings"
```

---

### Task 9: Pre-tag full validation

- [ ] **Step 1:** Push main; watch "Build, Test & Release" + verify green (now includes the unit-test steps).
- [ ] **Step 2:** `gh workflow run build.yml` (workflow_dispatch) to exercise **build-static** with the new dependency stack WITHOUT tagging; watch to green. This is the only CI path that compiles the vendored bumps.
- [ ] **Step 3:** Full warm-cache TV-fixture run + the standard ffprobe assertion set (title, av1 jpn, opus 6ch fre default, opus 6ch jpn, subrip fre, 4 chapters) + a fresh seek/keyframe spot-check on the output.

---

### Task 10: Release v2.0.0

- [ ] **Step 1:** `project(vmavificient VERSION 1.6.0 …)` → `2.0.0` in CMakeLists.txt:36; grep README.md for stale version strings and update if present.
- [ ] **Step 2:** Rebuild, confirm banner `vmavificient v2.0.0`, tests green.
- [ ] **Step 3:** Commit `chore(release): bump project version to 2.0.0`.
- [ ] **Step 4:** Annotated tag `v2.0.0` — message summarizes: dependency stack refresh (table above), FFmpeg 8 floor, SVT diagnostics visible, OCR preflight, CI hardening. Push `main` + tag together.
- [ ] **Step 5:** Watch the tag run to completion; verify the GitHub release has `vmavificient-macos-arm64` + `SHA256SUMS` and the tag↔version guard passed.

---

## Execution order

Task 1 (bumps + background vendored build) → Tasks 2, 3, 4 (code fixes on the fast system build, sequential) → Task 5 (vendored validation gate + deps commit) → Task 6 (CI hardening) → Task 7 (thin-main, own plan) → Task 8 (tidy + gate) → Task 9 (pre-tag validation) → Task 10 (release).

## Explicitly out of scope (v2.1+)

- **Linux CI:** still blocked on libdovi-dev distro availability (per build.yml comment).
- **Dep pins as immutable SHAs + dropping `-DCMAKE_CROSSCOMPILING=ON`:** design notes preserved in memory `project-repro-subprocess-branch`; revisit after v2.0.
