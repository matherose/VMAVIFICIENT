# CI Rework Design

**Date:** 2026-07-11
**Status:** Approved by Joel (design review in session)
**Replaces:** `.github/workflows/build.yml` (single-file CI, 419 lines, 7 jobs)

## Context and goals

The current `build.yml` grew organically around needs that no longer exist. A
review found:

- The setup preamble (checkout → brew → Rust → caches → cargo-c) is
  copy-pasted four times, and the brew package lists have drifted between
  copies.
- The `clang-tidy` job performs a full duplicate of the `build-macos-system`
  build solely to produce `compile_commands.json`.
- The `encode-test` job downloads the build artifact and re-downloads Big
  Buck Bunny on every run; it mostly exercises artifact transport, not a
  user-facing path. The sanitizer job runs the same smoke encode under ASAN.
- `-DCMAKE_CROSSCOMPILING=ON` appears on every configure line as a
  workaround that is no longer needed.
- Missing hygiene: no `concurrency` group, no top-level `permissions`,
  third-party actions pinned to mutable tags, no path filters, no linting of
  the workflow files themselves.

Goal: a professional, deduplicated CI split by trigger, preserving every
behavior that earned its place.

## Decisions (design Q&A)

1. **Shape:** split into `ci.yml` (push/PR) and `release.yml` (tags +
   dispatch), with a composite setup action for the shared preamble.
2. **encode-test:** merged into the build job; BBB cached via
   `actions/cache`; artifact upload/download round-trip removed.
3. **clang-tidy:** folded into the build job as a post-build step using the
   build's own `compile_commands.json`.
4. **Hygiene:** all four adopted — SHA-pinned actions, concurrency +
   least-privilege permissions, path filters, actionlint.
5. **Fold-ins:** both adopted — CMakeLists dep pins become immutable commit
   SHAs, and `-DCMAKE_CROSSCOMPILING=ON` is dropped everywhere.

## File layout

```
.github/
  workflows/
    ci.yml         push (main) + pull_request, path-filtered
    release.yml    tags v* + workflow_dispatch
  actions/
    setup-build/
      action.yml   composite: brew, Rust, cargo-c, caches
```

`build.yml` is deleted in the same change.

## ci.yml

Triggers: `push` to `main` and `pull_request`, both with `paths-ignore` for
`docs/**` and `**.md` (changes that cannot affect the build).

Top level: `concurrency` group keyed on workflow + ref with
`cancel-in-progress: true`; `permissions: contents: read`.

### Job: lint (ubuntu-latest)

- clang-format-18 check (apt), enumerating files exactly as today —
  fail-fast gate kept from the old `format-check` job.
- actionlint over `.github/workflows/*.yml` (catches expression typos and
  shellchecks `run:` blocks).

### Job: build-test (macos-14, needs: lint)

The old `build-macos-system`, `clang-tidy`, and `encode-test` jobs merged
into one linear job:

1. Composite setup in system-deps mode.
2. Configure + build (Release, system shared deps, no CROSSCOMPILING flag).
3. Unit tests (`vmav_tests`).
4. Banner check (`./build/vmavificient --help | head -1`).
5. clang-tidy against the build's `compile_commands.json` (brew llvm,
   `-clang-tidy-binary`, same invocation as today).
6. Smoke encode: `--blind --bitrate 2000` on Big Buck Bunny restored from
   `actions/cache` (keyed on the source URL), downloaded only on cache miss.

Consequence accepted at design review: a tidy or smoke failure reports as
"build-test failed"; the step name identifies the actual cause.

### Job: sanitizer (macos-14, needs: lint)

Unchanged in substance from today:

- Composite setup in vendored mode.
- Vendored Debug build with `-fsanitize=address,undefined`.
- ASAN smoke encode on the same cached BBB media (same cache key as
  build-test's).
- Tuned `ASAN_OPTIONS` kept verbatim.

## release.yml

Triggers: `push` on tags `v*`, plus `workflow_dispatch` as a dry-run
validator (replacing build-static's old dispatch role — the whole release
path runs without publishing, no throwaway tag needed).

Top level: same concurrency pattern; `permissions: contents: read` with the
publish job elevating to `contents: write` locally.

Single job (build and publish together; the publish step alone is
tag-gated, so no artifact hand-off between jobs is needed):

1. Composite setup in vendored mode.
2. Vendored static Release build.
3. **Tag↔version guard** (kept verbatim): tag name must match
   `project(... VERSION ...)` in CMakeLists.txt — this guard exists because
   the CLI banner once shipped three releases reporting v1.2.0.
4. Package `vmavificient-macos-arm64`, generate SHA256SUMS.
5. Publish via `softprops/action-gh-release` (SHA-pinned) — this step only,
   gated on `if: startsWith(github.ref, 'refs/tags/')`.

## Composite action: setup-build

Inputs: `mode` (`system` | `vendored`).

- Single brew package list per mode (one source of truth; ends the drift).
- Rust toolchain + cargo-c (vendored mode; needed for grav1synth).
- Caches:
  - ExternalProject deps tree: **exact-key only**, keyed on
    `hashFiles('CMakeLists.txt')` + variant. No `restore-keys` — a stale
    restored tree makes ExternalProject run `git update` in place, which
    bricked the sanitizer job when a pinned tag moved (fixed in 20ae0ec;
    the why-comment moves here).
  - Cargo registry: content-addressed, keeps its `restore-keys`.

## CMakeLists fold-ins

- Every vendored dep's `GIT_TAG` (~15 pins: FFmpeg, opus, libdovi, cjson,
  zlib, openssl, curl, libpng, libjpeg, libtiff, leptonica, tesseract,
  libvmaf, dav1d, SVT-AV1-HDR, libhdr10plus) becomes an immutable commit
  SHA, with the human-readable version kept as a trailing comment.
  Compatible with `GIT_SHALLOW` (GitHub/GitLab honor fetch-by-SHA). This
  also makes the exact-key deps caches genuinely immutable.
- `-DCMAKE_CROSSCOMPILING=ON` removed from every CI configure line; CI must
  stay green without it before the change lands.

## Hygiene details

- **SHA-pinning:** every third-party action (`actions/checkout`,
  `actions/cache`, `softprops/action-gh-release`, actionlint runner, etc.)
  pinned to a full commit SHA with `# vX.Y.Z` comment.
- **Concurrency:** `${{ github.workflow }}-${{ github.ref }}`,
  cancel-in-progress, both workflows.
- **Permissions:** top-level `contents: read`; only the publish job gets
  `contents: write`.
- **Path filters:** `paths-ignore: [docs/**, '**.md']` on ci.yml triggers.

## Preserved behaviors (do not lose)

- Ubuntu clang-format fail-fast gate before any macOS job runs.
- Exact-key ExternalProject caching keyed on CMakeLists.txt.
- Tag↔version guard and SHA256SUMS in the release path.
- Tuned ASAN_OPTIONS.
- The why-comments explaining each non-obvious choice (move, don't delete).

## Out of scope

- Linux/other-arch build targets (macOS arm64 remains the only release
  artifact).
- Homebrew tap automation.
- Any change to the pipeline code itself.

## Validation

- Both workflows pass actionlint locally before push.
- A push to main runs ci.yml fully green (lint → build-test + sanitizer),
  with BBB restored from cache on the second run.
- `workflow_dispatch` of release.yml runs green end-to-end with the publish
  step skipped.
- A docs-only commit triggers no ci.yml run.
- Cache behavior verified: same CMakeLists.txt → cache hit; the SHA-pin
  commit rotates the key once (expected cold build), then hits again.
