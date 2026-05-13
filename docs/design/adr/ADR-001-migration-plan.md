# Project Migration Plan for AGENTS.md Compliance

**Date:** 2026-05-13  
**Target:** Current project structure (flat) → AGENTS.md § 15 canonical layout  
**Version:** 1.0

---

## Current State Analysis

### Existing Structure

- **Source files:** All `.c` files directly in `src/` (18 files)
- **Header files:** All `.h` files directly in `include/` (17 files)
- **Build system:** Single `CMakeLists.txt` root file
- **Build artifacts:** `build/` directory with nested structure

### Existing Tooling (Compliant)

- [.clang-format](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/.clang-format) — LLVM base style, ColumnLimit 100
- [.clang-tidy](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/.clang-tidy) — Full check set with NASA Power of Ten enforcement
- [.gitmessage](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/.gitmessage) — Commit template with Assisted-by trailer format
- [CHANGELOG.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/CHANGELOG.md) — Semantic versioning log

### Missing Tooling (Required by AGENTS.md)

1. **CMakePresets.json** — Build presets (debug, asan, msan, tsan, coverage, release)
2. [.gitattributes](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/.gitattributes) — Line ending + binary detection
3. [.pre-commit-config.yaml](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/.pre-commit-config.yaml) — Pre-commit hooks
4. [Doxyfile](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/Doxyfile) — Doxygen configuration (WARN_AS_ERROR=YES)
5. [SECURITY.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/SECURITY.md) — Vulnerability disclosure policy

---

## Migration Tasks

### Phase 1: Directory Structure (Tasks 3-10, 12)

#### Task 3: Create Project Directory Structure

**Status:** Pending  
**Action:**

```bash
mkdir -p src/{audio_encode,config,crf_search,encode_preset,final_mux,media_analysis,media_crop,media_hdr,media_info,media_naming,media_tracks,rpu_extract,subtitle_convert,tmdb,ui,utils,video_encode}
mkdir -p tests/unit
mkdir -p fuzz/corpus
mkdir -p cmake
mkdir -p scripts
mkdir -p docs/{api,design/adr}
mkdir -p .a5c/processes
mkdir -p reports/{analyzer,coverage}
```

**Success criterion:** All directories exist at project root.

---

#### Task 4: Configure CMakePresets.json

**Status:** Pending  
**Action:** Create `CMakePresets.json` with all 7 presets per AGENTS.md § 8.3

**Success criterion:** All presets configure and build successfully:

```bash
cmake --preset debug && cmake --build build/debug
cmake --preset asan     && cmake --build build/asan
cmake --preset msan     && cmake --build build/msan
cmake --preset tsan     && cmake --build build/tsan
cmake --preset coverage && cmake --build build/coverage
cmake --preset release  && cmake --build build/release
```

---

#### Task 5: Create CMake Infrastructure Files

**Status:** Pending  
**Action:** Create in `cmake/`:

- [CompilerFlags.cmake](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/cmake/CompilerFlags.cmake) — Flag sets from AGENTS.md § 8.2
- [Sanitizers.cmake](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/cmake/Sanitizers.cmake) — Sanitizer preset helpers
- [CodeCoverage.cmake](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/cmake/CodeCoverage.cmake) — llvm-cov workflow
- [Criterion.cmake](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/cmake/Criterion.cmake) — Test framework integration

**Success criterion:** CMake finds and includes all helper files without errors.

---

#### Task 6: Create Project-Level AGENTS.md

**Status:** Pending  
**Action:** Create `AGENTS.md` at project root that:

- References global AGENTS.md at `~/.pi/agent/AGENTS.md`
- Documents any project-specific deviations with rationale
- Includes this migration plan as "Project Migration Status"

**Success criterion:** Project-level AGENTS.md exists and references global correctly.

---

#### Task 7: Create Missing Tooling Files

**Status:** Pending  
**Action:** Create at project root:

- [.gitattributes](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/.gitattributes) — Canonical entries from AGENTS.md § 15.4
- [.pre-commit-config.yaml](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/.pre-commit-config.yaml) — Hooks per AGENTS.md § 19.4
- [Doxyfile](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/Doxyfile) — WARN_AS_ERROR=YES, OUTPUT_DIRECTORY=docs/api
- [SECURITY.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/SECURITY.md) — Minimum content from AGENTS.md § 19.5

**Success criterion:** All files exist and pass validation:

```bash
doxygen Doxyfile 2>&1 | grep -E "warning:|error:"  # Should be quiet
```

---

#### Task 8: Update CI/CD Workflow

**Status:** Pending  
**Action:** Update `.github/workflows/build.yml` to match AGENTS.md § 18 workflow:

- Add `lint` job (clang-tidy) after `format-check`
- Add `asan`, `msan`, `tsan` sanitizer jobs
- Add `coverage` job with threshold enforcement
- Add `docs` job (Doxygen zero warnings)
- Add `ci-pass` gate requiring all above jobs

**Success criterion:** CI runs pass with new workflow structure.

---

#### Task 9: Create Project Scripts

**Status:** Pending  
**Action:** Create in `scripts/`:

- [check.sh](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/scripts/check.sh) — Full pipeline: fmt → tidy → build → asan → msan → cov → docs
- [coverage.sh](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/scripts/coverage.sh) — Coverage build, merge, HTML report
- [fuzz.sh](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/scripts/fuzz.sh) — Run all fuzz targets
- [rotate-models.sh](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/scripts/rotate-models.sh) — Update AGENTS.md § 14 with new model IDs

**Success criterion:** All scripts are executable and idempotent.

---

#### Task 10: Create Documentation Structure

**Status:** Pending  
**Action:**

- [docs/api/](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/docs/api/) — Doxygen output (GITIGNORED)
- [docs/design/architecture.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/docs/design/architecture.md) — System overview
- [docs/design/adr/](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/docs/design/adr/) — Architecture Decision Records
- [docs/false-positives.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/docs/false-positives.md) — Documented static analysis suppressions

**Success criterion:** Doxygen generates no warnings.

---

#### Task 12: Create Fuzz Infrastructure

**Status:** Pending  
**Action:** Create in `fuzz/`:

- `fuzz_main.c` — libFuzzer entry point
- Per parser/input handler targets (e.g., [fuzz_config.c](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/fuzz/fuzz_config.c), [fuzz_subtitle.c](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/fuzz/fuzz_subtitle.c))
- Seed corpus in `fuzz/corpus/<module>/`

**Success criterion:** Fuzz targets compile and run without crashes.

---

### Phase 2: Source Migration (Tasks 11, 13)

#### Task 11: Create Test Infrastructure

**Status:** Pending  
**Action:**

- [tests/unit/CMakeLists.txt](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/tests/unit/CMakeLists.txt) — Test framework setup
- Create test files mirroring `src/` modules
- Add to main tests/CMakeLists.txt

**Success criterion:** Tests pass with debug and asan presets.

---

#### Task 13: Migrate Source Files to Modules

**Status:** Pending  
**Action:** Move files:

- `src/audio_encode.c` → `src/audio_encode/audio_encode.c`
- `src/config.c` → `src/config/config.c`
- [Insert all 18 source files...]
- Similarly for headers in `include/`

Create per-module CMakeLists.txt in `src/<module>/`

**Success criterion:** Build passes after migration.

---

## Compliance Checklist

### AGENTS.md § 15 Requirements

- [ ] [AGENTS.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/AGENTS.md) at project root (references global)
- [ ] [README.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/README.md) (already present)
- [ ] [CHANGELOG.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/CHANGELOG.md) (already present)
- [ ] `LICENSE` (already present)
- [ ] [SECURITY.md](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/SECURITY.md) (new)
- [ ] `.gitmessage` (already present, correct format)
- [ ] `.clang-format` (already present)
- [ ] `.clang-tidy` (already present)
- [ ] `.gitignore` (already present)
- [ ] `.gitattributes` (new)
- [ ] `.pre-commit-config.yaml` (new)
- [ ] [Doxyfile](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/Doxyfile) (new)
- [ ] [CMakeLists.txt](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/CMakeLists.txt) (existing, to be modularized)
- [ ] [CMakePresets.json](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/CMakePresets.json) (new)

### AGENTS.md § 11 Build System

- [ ] `cmake/` directory with helper files
- [ ] [scripts/](file:///Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/scripts/) with check.sh, coverage.sh, fuzz.sh, rotate-models.sh

### AGENTS.md § 15.1 Directory Layout

- [ ] `include/<project>/` — Public API (namespaced)
- [ ] `src/<module>/` — Modular structure
- [ ] `tests/unit/` — Unit tests
- [ ] `tests/integration/` — Integration tests
- [ ] `fuzz/` — Fuzz targets
- [ ] `cmake/` — CMake modules
- [ ] `scripts/` — Project scripts
- [ ] `docs/` — Documentation

### CI/CD (AGENTS.md § 18)

- [ ] `format` job (clang-format)
- [ ] `lint` job (clang-tidy)
- [ ] `build-test` job (debug + tests)
- [ ] `asan` job
- [ ] `msan` job
- [ ] `tsan` job
- [ ] `coverage` job with thresholds
- [ ] `docs` job (Doxygen)
- [ ] `ci-pass` gate

---

## Estimated Impact

### Files Created: ~25

### Files Modified: ~5

### Breaking Changes: None

### Risk Assessment

- **Low risk:** Directory structure changes are additive; existing flat structure can be preserved temporarily
- **Medium risk:** CMake migration requires testing across all presets
- **High impact:** All new files follow canonical AGENTS.md structure

---

## Rollback Plan

If migration fails:

1. Keep current structure
2. Use git to revert directory changes
3. Continue with patch updates to existing `CMakeLists.txt`

---

**Next Step:** Approve task sequence, then execute Phase 1 tasks in order (3-10, 12).
