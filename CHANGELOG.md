# Changelog

All notable changes to vmavificient are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] — 2026-04-29

System-deps refactor. The build now uses system-provided shared libraries by
default; only the three deps that aren't packaged anywhere (SVT-AV1-HDR,
libhdr10plus, grav1synth) still build from source. This is the foundation
for distro packaging — `.deb`, brew formula, AUR — and brings build times
from ~30 min to ~3 min on a clean checkout.

### Build / packaging

- New CMake option **`VMAV_USE_SYSTEM_DEPS`** (default `ON`):
  - **`ON`**: use `pkg_check_modules` / `find_package` for FFmpeg (6 libs),
    Opus, libdovi, Tesseract, Leptonica, libpng/jpeg/tiff, zlib, OpenSSL,
    libcurl, cJSON. The default and recommended path.
  - **`OFF`**: reproduce the v1.0.x build that vendors and statically links
    every dependency. Used by the GitHub-release static-binary artifact.
- Three deps stay vendored regardless of the toggle:
  - **SVT-AV1-HDR** (juliobbv-p fork) — Debian only ships upstream SVT-AV1;
    we need the HDR fork's `--noise` and DV/HDR10+ passthrough.
  - **libhdr10plus** — not in Debian; Homebrew formula installs the binary
    without the library.
  - **grav1synth** — CLI tool, invoked as a subprocess.
- Build size dropped from ~38 MB (vendored) to ~4 MB (system) on macOS arm64.

### CI

- New **`build-linux-system`** job (Ubuntu 24.04 + apt) — first-class Linux
  support, replacing the previous "aspirational" status.
- Renamed `build` → **`build-macos-system`**: uses Homebrew + system deps,
  runs on every push. Cuts CI time from ~30 min to ~5 min.
- New **`build-static`** job: vendored mode for the GitHub-release artifact,
  runs only on tag pushes (`refs/tags/v*`) and `workflow_dispatch`. Caches
  `build/deps/` keyed on `hashFiles('CMakeLists.txt')`.
- The **`sanitizer`** job stays on vendored mode so ASAN can interpose our
  static archives without bumping into Homebrew's signed dynamic libs.

### Documentation

- README rewritten with per-platform install commands (macOS Homebrew +
  Debian/Ubuntu apt) and a section explaining when to use
  `-DVMAV_USE_SYSTEM_DEPS=OFF` for the static-binary build.
- `CMakeLists.txt` reorganized for clarity: always-vendored deps first,
  then a single `if(VMAV_USE_SYSTEM_DEPS)` switch separating the two
  branches.

### Roadmap

- **v1.2.0** — CPack `DEB` generator on top of v1.1.0; bundles the three
  vendored libs as static archives in the .deb. AUR + a Homebrew formula
  to follow.

## [1.0.1] — 2026-04-29

Polish + correctness pass on top of the v1.0.0 release. No behavior changes
to the encoder pipeline, no preset retuning, no new flags. Pure stabilization.

### Security

- **Fixed shell-injection in the subtitle extract path** (`src/main.c`). The
  `ffmpeg` invocation that extracts text subtitles built its command string
  with naked `"%s"` substitutions, so a source filename containing
  backticks, `$(…)`, or unescaped quotes would have executed inside the
  shell. Both the input filepath and the output SRT path are now passed
  through `shell_quote_append()`. The helper was already in `final_mux.c`
  for the same reason; it has moved to `utils.c` so every call site shares
  one implementation.

### Fixed

- Wrap `system()` return codes with `WIFEXITED` / `WEXITSTATUS` in
  `final_mux` and the subtitle-extract path. A child killed by a signal no
  longer reports its termination signal as a "normal" exit code.
- Replace the variable-length stack array in
  `select_best_audio_per_language()` (`src/media_tracks.c`) with a
  heap-allocated buffer. VLAs are not portable to MSVC, which matters for
  the planned Windows port.

### Added

- The CLI banner now prints the project version:
  `vmavificient v1.0.1 — SVT-AV1-HDR <ver>`. The version comes from
  `PROJECT_VERSION` in `CMakeLists.txt` and is exposed to source as
  `VMAV_VERSION`.
- `-DVMAV_SANITIZE=ON` CMake option turns on AddressSanitizer +
  UndefinedBehaviorSanitizer with `-O1 -g -fno-omit-frame-pointer`, used
  by the new CI `sanitizer` job for catching memory bugs before release.
- `CHANGELOG.md` (this file).

### CI

- `format-check` job enforces `clang-format --dry-run -Werror` on every push;
  the project's `.clang-format` config has existed since 1.0.0 but was not
  gating CI until now.
- `sanitizer` job builds with `VMAV_SANITIZE=ON` and runs the BBB smoke
  encode under ASAN/UBSAN. Independent of the release artifact build.
- `actions/cache@v4` caches `build/deps/` (FFmpeg / SVT-AV1-HDR / Tesseract /
  …) keyed on `hashFiles('CMakeLists.txt')`. Bumping a dep version
  invalidates the cache automatically; otherwise CI saves ~30 minutes per
  run.
- The `release` job emits a `SHA256SUMS` file alongside the binary so
  downloaders can verify integrity with `shasum -c`.
- Smoke-test bitrate raised from 500 → 2000 kbps so the rate-control path
  is actually exercised on the HD live-action clean tier floor.

### Repository

- `.gitignore` trimmed from 699 lines of toptal-template boilerplate down
  to ~80 lines covering exactly what this project produces. Each section
  has a short comment explaining why it's listed.

### Roadmap

- **v1.1.0** will move the system-available dependencies (FFmpeg, Opus,
  libdovi, Tesseract+Leptonica, libpng/jpeg/tiff, zlib, OpenSSL, libcurl,
  cJSON) from `ExternalProject_Add` to `pkg_check_modules` /
  `find_package`. SVT-AV1-HDR (juliobbv-p fork), libhdr10plus, and
  grav1synth stay vendored — they aren't packaged in Debian. A
  `VMAV_USE_SYSTEM_DEPS` toggle preserves the all-vendored build for
  the GitHub-release static-binary path.
- **v1.2.0** adds CPack `DEB` packaging on top of the system-deps build.

## [1.0.0] — 2026-04-28

First stable release. Targets the HDLight / 4KLight scene-release tier:
source file in, fully muxed MKV out, with Dolby Vision / HDR10+ / HDR10 /
subtitles / chapters preserved.

### Encoder

- SVT-AV1-HDR 4.1.0 "Chromedome" with full Dolby Vision RPU passthrough.
- Dual grain mechanism: `--noise` (synthetic overlay, content-agnostic,
  zero overhead) for digital + animation; `--film-grain` (analyzed +
  denoised) for analog film.
- Flat tier-based bitrate table, no perceptual search:

  | Tier      | Grainy | Clean | Animation |
  |-----------|--------|-------|-----------|
  | 4KLight   | 4000   | 3500  | 3000      |
  | HDLight   | 2500   | 2000  | 1500      |

- Six quality presets: live-action, animation, Super 35 analog/digital,
  IMAX analog/digital. Analog film tiers use tune 5 (the Film Grain tune).

### Pipeline

- Multi-window grav1synth-based grain analysis with adaptive refinement.
- ffmpeg cropdetect for letterbox / pillarbox.
- Dolby Vision RPU NAL extraction (HEVC UNSPEC62).
- Per-track Opus encoding (full VBR, 56 kbps/channel, multichannel mapping
  for 5.1 / 7.1).
- PGS bitmap → SRT via Tesseract OCR.
- TMDB lookup + scene-release naming (MULTi.VFI / VFF / VFQ / VOF, DUAL.VFI,
  FRENCH, TRUEFRENCH, etc.).

### CLI

- Section-based output with stage status lines, contextual error hints,
  per-window grain progress feedback.
- Flags: `--dry-run`, `--grain-only`, `--quiet`, `--verbose`, `--animation`,
  `--super35_analog/digital`, `--imax_analog/digital`, `--bitrate`, `--srt`,
  `--tmdb`, `--blind`, plus a full set of language and source type
  overrides.
- Honors `NO_COLOR=1` and `VMAV_KEEP_GRAIN_TMP=1`.

### Build

- Single CMake/Ninja invocation; all major dependencies built from source
  via ExternalProject (FFmpeg, SVT-AV1-HDR, libdovi, libhdr10plus, Opus,
  Tesseract, grav1synth).
- macOS arm64 supported; Linux aspirational.

[1.1.0]: https://github.com/matherose/VMAVIFICIENT/releases/tag/v1.1.0
[1.0.1]: https://github.com/matherose/VMAVIFICIENT/releases/tag/v1.0.1
[1.0.0]: https://github.com/matherose/VMAVIFICIENT/releases/tag/v1.0.0
