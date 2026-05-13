# Migration Plan for VMAVIFICIENT - AGENTS.md Compliance

**Date:** 2026-05-13  
**Author:** Assistant (pi/tier2:Qwen/Qwen2.5-Coder-32B-Instruct)  
**Status:** Ready for execution  
**Related ADR:** ADR-001-migration-plan.md

---

## Executive Summary

The VMAVIFICIENT project needs to be restructured from its current flat layout
to the canonical AGENTS.md § 15 directory structure. This migration will enable
compliance with the global agent contract rules for C development, including:

- Modular source organization per NASA Power of Ten principles
- Full CMake preset infrastructure for all build configurations
- Complete CI/CD pipeline with sanitizers, coverage, and documentation gates
- Standardized tooling across all projects using this AGENTS.md

---

## Current State

### Directory Structure (Flat - Non-Compliant)

```
project-root/
├── AGENTS.md (global, not project-level)
├── README.md
├── CHANGELOG.md
├── LICENSE
├── .clang-format ✓
├── .clang-tidy ✓
├── .gitmessage ✓
├── CMakeLists.txt (single monolithic file)
├── .github/workflows/build.yml (partial CI)
├── include/ (17 .h files directly)
│   ├── audio_encode.h
│   ├── config.h
│   └── ... (15 more)
└── src/ (18 .c files directly)
    ├── audio_encode.c
    ├── config.c
    └── ... (16 more)
```

### Missing Required Files

| File                    | Purpose                        | AGENTS.md § |
| ----------------------- | ------------------------------ | ----------- |
| CMakePresets.json       | Build preset configurations    | 8.3         |
| .gitattributes          | Line ending + binary detection | 15.4        |
| .pre-commit-config.yaml | Pre-commit hooks               | 19.4        |
| Doxyfile                | Doxygen configuration          | 8.6         |
| SECURITY.md             | Vulnerability disclosure       | 19.5        |
| docs/                   | Documentation directory        | 15.1        |

---

## Target State (Canonical)

```
project-root/
├── AGENTS.md (PROJECT-LEVEL, references global)
├── README.md ✓
├── CHANGELOG.md ✓
├── LICENSE ✓
├── SECURITY.md (NEW)
├── .gitmessage ✓
├── .clang-format ✓
├── .clang-tidy ✓
├── .gitignore ✓
├── .gitattributes (NEW)
├── .pre-commit-config.yaml (NEW)
├── Doxyfile (NEW)
├── CMakeLists.txt (MODULARIZED)
├── CMakePresets.json (NEW)
├── .github/workflows/ (FULL CI)
│   ├── ci.yml (NEW)
│   └── nightly-fuzz.yml (NEW)
├── include/<project>/ (namespaced public API)
├── src/<module>/ (modular structure)
│   ├── audio_encode/
│   ├── config/
│   └── ... (16 more)
├── tests/unit/ (NEW)
├── tests/integration/ (NEW)
├── fuzz/ (NEW)
│   └── corpus/<module>/
├── cmake/ (NEW)
│   ├── CompilerFlags.cmake
│   ├── Sanitizers.cmake
│   ├── CodeCoverage.cmake
│   └── Criterion.cmake
├── scripts/ (NEW)
│   ├── check.sh
│   ├── coverage.sh
│   ├── fuzz.sh
│   └── rotate-models.sh
├── docs/ (NEW)
│   ├── api/ (Doxygen output, GITIGNORED)
│   ├── design/
│   │   ├── architecture.md
│   │   └── adr/
│   │       ├── ADR-001-migration-plan.md (THIS FILE)
│   │       └── ...
│   └── false-positives.md
├── .a5c/processes/ (NEW)
└── build/ (preserved)
    ├── debug/
    ├── asan/
    ├── msan/
    ├── tsan/
    ├── coverage/
    └── release/
```

---

## Migration Tasks (In Execution Order)

### Phase 1: Infrastructure Setup

#### Task 1.1: Create Project-Level AGENTS.md

**Priority:** HIGH  
**Description:** Create project-level AGENTS.md that references the global file
and documents any deviations.

**Files to create:**

- `AGENTS.md` at project root

**Execution:**

```bash
# Copy global AGENTS.md to project root
cp ~/.pi/agent/AGENTS.md AGENTS.md

# Update preamble to indicate this is project-level
# Add "Project Status" section documenting migration in progress
```

---

#### Task 1.2: Create CMakePresets.json

**Priority:** HIGH  
**Description:** Create build preset configurations for all required build types.

**Files to create:**

- `CMakePresets.json` at project root

**Configuration:** See AGENTS.md § 8.3 for full preset specification.

---

#### Task 1.3: Create CMake Infrastructure

**Priority:** HIGH  
**Description:** Create helper CMake modules for flags, sanitizers, coverage, and tests.

**Files to create:**

- `cmake/CompilerFlags.cmake`
- `cmake/Sanitizers.cmake`
- `cmake/CodeCoverage.cmake`
- `cmake/Criterion.cmake`

**Dependencies:** Requires CMakePresets.json

---

#### Task 1.4: Create Missing Tooling Files

**Priority:** HIGH  
**Description:** Create configuration files for formatting, linting, and documentation.

**Files to create:**

- `.gitattributes`
- `.pre-commit-config.yaml`
- `Doxyfile`
- `SECURITY.md`

**Dependencies:** None

---

#### Task 1.5: Update CI/CD Workflow

**Priority:** HIGH  
**Description:** Expand CI workflow to include all quality gates from AGENTS.md § 18.

**Files to create:**

- `.github/workflows/ci.yml`
- `.github/workflows/nightly-fuzz.yml`

**Dependencies:** Complete CMake infrastructure

---

#### Task 1.6: Create Project Scripts

**Priority:** MEDIUM  
**Description:** Create helper scripts for development workflow.

**Files to create:**

- `scripts/check.sh`
- `scripts/coverage.sh`
- `scripts/fuzz.sh`
- `scripts/rotate-models.sh`

**Dependencies:** None

---

#### Task 1.7: Create Documentation Structure

**Priority:** MEDIUM  
**Description:** Set up documentation directories and initial content.

**Directories to create:**

- `docs/api/`
- `docs/design/`
- `docs/design/adr/`
- `docs/false-positives.md`

**Dependencies:** Doxyfile creation

---

#### Task 1.8: Create Test Infrastructure

**Priority:** HIGH  
**Description:** Set up unit test framework with Criterion.

**Files to create:**

- `tests/unit/CMakeLists.txt`
- `tests/unit/test_<module>.c` (one per module)
- `tests/integration/CMakeLists.txt`
- `fuzz/` (initial structure)

**Dependencies:** CMake infrastructure

---

#### Task 1.9: Create Fuzz Infrastructure

**Priority:** MEDIUM  
**Description:** Set up libFuzzer targets for input parsers.

**Files to create:**

- `fuzz/fuzz_<module>.c` (one per parser/input handler)
- Seed corpus in `fuzz/corpus/<module>/`

**Dependencies:** Test infrastructure

---

### Phase 2: Source Code Migration

#### Task 2.1: Create Module Directories

**Priority:** HIGH  
**Description:** Create modular directory structure under src/ and include/.

**Action:**

```bash
mkdir -p src/{audio_encode,config,crf_search,encode_preset,\
              final_mux,media_analysis,media_crop,media_hdr,\
              media_info,media_naming,media_tracks,rpu_extract,\
              subtitle_convert,tmdb,ui,utils,video_encode}

mkdir -p include/vmavificient/{audio_encode,config,crf_search,\
                                encode_preset,final_mux,media_analysis,\
                                media_crop,media_hdr,media_info,\
                                media_naming,media_tracks,rpu_extract,\
                                subtitle_convert,tmdb,ui,utils,video_encode}
```

**Dependencies:** None

---

#### Task 2.2: Migrate Source Files

**Priority:** HIGH  
**Description:** Move each module's source and header files to their new directories.

**Files to migrate:** 18 source files + 17 header files

**Action per module:**

1. Move `src/audio_encode.c` → `src/audio_encode/audio_encode.c`
2. Move `include/audio_encode.h` → `include/vmavificient/audio_encode/audio_encode.h`
3. Create `src/audio_encode/CMakeLists.txt`
4. Update `#include` paths in dependent files

**Dependencies:** Module directories created

---

#### Task 2.3: Update Main CMakeLists.txt

**Priority:** HIGH  
**Description:** Refactor monolithic CMakeLists.txt into modular structure.

**Changes needed:**

- Remove direct source list, use `add_subdirectory()` for each module
- Add `add_subdirectory(tests/unit)`
- Add `add_subdirectory(fuzz)` (if present)
- Include cmake/\* helper files

**Dependencies:** Source files migrated to modules

---

## Compliance Verification

### Post-Migration Checklist

####道路 Structure

- [ ] Every `.c` file in `src/<module>/`
- [ ] Every `.h` file in `include/<project>/`
- [ ] No files directly in `src/` or `include/`

#### Build System

- [ ] `cmake --preset debug` succeeds
- [ ] `cmake --preset asan` succeeds
- [ ] `cmake --preset msan` succeeds
- [ ] `cmake --preset tsan` succeeds
- [ ] `cmake --preset coverage` succeeds
- [ ] `cmake --preset release` succeeds

#### Quality Gates

- [ ] `clang-format --dry-run --Werror` passes
- [ ] `clang-tidy -p build/debug` passes (zero findings)
- [ ] All tests pass in debug preset
- [ ] All tests pass in asan preset
- [ ] Coverage thresholds met (80% line, 75% branch, 90% function)

#### Documentation

- [ ] Doxygen generates no warnings
- [ ] `docs/api/` contains generated documentation
- [ ] All public symbols have Doxygen comments

#### CI/CD

- [ ] All CI jobs succeed
- [ ] No warnings or errors in any sanitizer build

---

## Risk Mitigation

### Parallel Structure Retention

During migration, the flat structure in `src/` will be gradually phased out.
Old files can be deleted once migrations to `src/<module>/` are complete and
verified.

### Git Workflow

All changes should be made in a feature branch until migration is complete:

```bash
git checkout -b feature/migration-agents-md
# ... make changes ...
git commit -m "refactor(build): create CMakePresets.json for AGENTS.md compliance"
# ... merge after verification ...
```

### Rollback Plan

If any phase fails:

1. Keep the repository in its current state
2. Revert changes using `git revert` or `git reset`
3. Document the failure in the migration plan
4. Adjust the plan and retry

---

## Success Criteria

Migration is complete when:

1. All required files from AGENTS.md § 15 exist
2. CMake builds all presets without errors
3. All quality gates pass (clang-format, clang-tidy, Doxygen, tests)
4. CI/CD workflow passes with all jobs green
5. Documentation is complete and up to date

---

## Estimated Timeline

- **Phase 1: Infrastructure** - 4-6 hours
- **Phase 2: Source Migration** - 2-4 hours
- **Verification & Testing** - 2-4 hours
- **Total** - 8-14 hours (1-2 days with breaks)

---

## Next Steps

1. **Review this plan** - Ensure all tasks are correctly scoped
2. **Create task sequence** - Order tasks based on dependencies
3. **Execute Phase 1** - Start with infrastructure setup
4. **Execute Phase 2** - Migrate source files
5. **Verify compliance** - Run all quality gates
6. **Merge to main** - Update CI protection rules

---

**Approval Required:** Human approval before executing Phase 2 (source migration)

**Questions?** Ask clarifying questions about:

- Module boundaries for migration
- Any deviations from AGENTS.md requirements
- Priority ordering of tasks
- Risk tolerance for specific changes
