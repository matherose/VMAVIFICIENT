# Changelog

All notable changes to vmavificient are documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Pending for v2.0.0

- DV RPU OBU injection into the AV1 bitstream (currently extracted as
  a `.rpu.bin` sidecar but not yet woven into the encoded stream).
- Trim `.wixpdb` debug symbols from release artifacts.
- Real-install smoke tests on clean VMs per platform.

## [2.0.0-rc1] — 2026-05-27

First release candidate of v2.0.0. Clean-slate rewrite of v1.5.0
targeting professional-grade UHD HDR Blu-ray AV1 encoding.

### Added

#### Encoder
- In-process SVT-AV1-HDR via libSvtAv1Enc (replaces v1's
  `ffmpeg -c:v libsvtav1` subprocess).
- VMAF-targeted CRF search via libvmaf (replaces v1's two-pass
  `ffmpeg -lavfi libvmaf=...` subprocesses); deterministic curve,
  unit-tested.
- libdav1d for AV1 decode (used by CRF search to score samples
  in the same process).
- libopus for audio re-encode (replaces v1's `opusenc` subprocess).
- Dolby Vision Profile 8.1 RPU extraction via libdovi (`.rpu.bin`
  sidecar lands next to the IVF; AV1-OBU injection deferred).
- HDR10 passthrough: mastering display + content light level
  round-trip through SVT-AV1-HDR's non-spec scales + byte-swap.
  Validated on V_for_Vendetta 4K HDR Blu-ray rip.

#### Pipeline
- Resumable encode pipeline driven by JSON state at
  `cache-dir/state.json`. Every step (probe → tmdb → crop → grain
  → CRF → audio → subs → video → mux) writes status + outputs;
  resume picks up at the first non-COMPLETE step.
- N-way PTS merge muxer (`final_mux.c`) replaces v1's ffmpeg
  subprocess. Fixes the muted-audio bug from v1.

#### CLI parity with v1
- All v1 flags ported: `--preset <one of 6>` + shorthand
  (`--animation`, `--super35_analog`, etc.), `--target-vmaf`,
  `--crf-min`/`--crf-max`, `--crf`, `--bitrate` VBR, `--tmdb <id>`,
  `--blind`, `--config` interactive setup, source + language
  overrides (16 lang tags + 5 source tags), `--dry-run` plan
  preview, `--grain-only` preset knob dump, `--quiet`/`--verbose`,
  `--srt <path>` sidecar attach (repeatable), `--scale-to-hd`
  1920p downscale, `--companion-hd` dual UHD+HD output.

#### Real-content CI
- Big Buck Bunny trailer fixture (1080p, fetched from Blender CDN,
  trimmed to 5s with ffmpeg).
- Synthetic UHD HDR10 fixture (3840×2160 HEVC with real SEI
  mastering display + MaxCLL/MaxFALL via libx265).
- 5 integration tests asserting per-step state.json fields +
  ffprobe output (codec, dims, color metadata).
- Real-content tests gated on main/PR/tag refs; feature branches
  skip via `TEST_IGNORE` and `VMAV_REAL_CONTENT_ENABLED=0`.

#### Packaging
- 4-platform release artifacts: `.tar.zst` (any) + `.deb`
  (linux-x86_64, linux-aarch64) + `.pkg` (macos-arm64) + `.msi`
  (windows-x86_64).
- Reproducible builds: SOURCE_DATE_EPOCH + prefix-maps + LLD
  `--build-id=none` `--sort-section=name`. Strong-mode CI proves
  it via double-build sha256 compare on every main/tag push.
- Tessdata (`eng.traineddata`) bundled with every package at
  `share/vmavificient/tessdata/`. Pinned to
  tesseract-ocr/tessdata@4.1.0, sha256-validated.

#### CI/CD
- Single unified `.github/workflows/pipeline.yml` with 5 stages
  (lint → build → test → package → publish), matrix-driven over
  4 arches.
- Trigger gates: feature branch = lint+build+test (lean);
  PR-to-main = full chain except publish; main push = same;
  tag push = full chain including draft GH Release.
- Windows .msi packaging runs on `windows-latest` with native
  WiX (Linux dotnet+WiX is unsupported by the toolset — its
  BundleValidator has a `C:\\`-prefix path check that fails on
  every Linux invocation).

### Changed (vs v1)

- **Toolchain consolidation**: Clang-only via apt (Linux) + Apple
  Clang (macOS) + llvm-mingw (Windows cross from Linux). No GCC,
  no Intel Mac CI.
- **glibc-dynamic on Linux** (not statically linked). `.deb`
  declares `Depends: libc6 (>= 2.31)`. Decision made after two
  failed musl-static attempts (zig cc + apt+sysroot, May 2026).
- **Subcommand CLI**: `vmavificient encode | analyze | search |
  doctor | version | help`. Backward-compat: `vmavificient <input>`
  still dispatches to `encode`.
- **JSON state** (`state.json`) replaces v1's per-step adhoc
  cache files.

### Fixed (during v2 development, vs v1's behavior)

- Muted-audio-after-mux bug (v1: ffmpeg subprocess ordering).
- French SRT filename double-dot (`fre.fr..full.srt` → `fre.fr.full.srt`).
- HDR10 mastering display + CLL passthrough (v1 dropped them).
- Audio filename collision when two same-language tracks exist.

### Removed

- Every ffmpeg subprocess call (in-process libav* + libsvtav1
  + libvmaf + libopus instead).
- v1's musl-static binary target (replaced with glibc-dynamic
  per saved toolchain decision).
- v1's Intel Mac CI runner (Apple is winding down x86 hardware
  on GitHub Actions).

### Known limitations

- DV RPU is extracted but not yet injected into the AV1 OBU
  stream. UHD HDR10 works fully; full DV is post-v2.0 work.
- macOS `.pkg` outer is not byte-reproducible (`pkgbuild`
  embeds Apple-internal timestamps). The contained binary IS
  reproducible.
- `.wixpdb` debug symbols got captured in the release upload
  pattern. Cosmetic; will be trimmed in a polish PR.
- `vmavificient analyze` / `search` are partial — `analyze`
  doesn't recognize `--help`; `search` only does `--id` lookup
  (no title-based search yet).

## [1.5.0] — see `main` branch

Last v1 release. History preserved on the `main` branch.
