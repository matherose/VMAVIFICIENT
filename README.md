# vmavificient

Professional-grade AV1 encoding CLI for UHD HDR Blu-ray sources.
VMAF-targeted CRF search, HDR10 passthrough, in-process SVT-AV1-HDR
+ libvmaf + libopus + libdav1d (no ffmpeg subprocess), PGS subtitle
OCR via Tesseract, fully resumable pipeline via on-disk state.

> **Status — v2.0.0-rc1.** First release candidate of the v2 line.
> Test releases live at [GitHub Releases](https://github.com/matherose/VMAVIFICIENT/releases).
> Feedback welcome on the [issue tracker](https://github.com/matherose/VMAVIFICIENT/issues).

## Platforms

| OS      | Architecture | Toolchain                 | Status |
|---------|--------------|---------------------------|--------|
| Linux   | x86_64       | apt clang-20 + glibc      | CI ✓   |
| Linux   | aarch64      | apt clang-20 + glibc      | CI ✓   |
| macOS   | arm64        | Apple Clang               | CI ✓   |
| Windows | x86_64       | llvm-mingw (cross from Linux) | CI ✓   |

Binaries are glibc-dynamic on Linux (`libc6 >= 2.31`); vendored
third-party deps (SVT-AV1-HDR, FFmpeg, libvmaf, Tesseract, libdav1d,
libopus, libcurl, openssl, etc.) are statically linked. Single
binary per platform plus a 22 MB `eng.traineddata` for PGS OCR.

## Install

Download the artifact for your platform from
[GitHub Releases](https://github.com/matherose/VMAVIFICIENT/releases).

```sh
# Debian / Ubuntu amd64
sudo dpkg -i vmavificient_2.0.0-rc1_amd64.deb

# Debian / Ubuntu arm64
sudo dpkg -i vmavificient_2.0.0-rc1_arm64.deb

# macOS Apple Silicon — unsigned, see docs/gatekeeper-bypass.md
xattr -d com.apple.quarantine vmavificient-2.0.0-rc1-macos-arm64.pkg
sudo installer -pkg vmavificient-2.0.0-rc1-macos-arm64.pkg -target /

# Windows x86_64 — unsigned, see docs/smartscreen-bypass.md
# Double-click the .msi; SmartScreen → More info → Run anyway.

# Portable tarball (any of the 4 arches)
zstd -dc vmavificient-2.0.0-rc1-<arch>.tar.zst | tar -xf -
./bin/vmavificient version
```

## Quickstart

```sh
# Full encode (TMDB lookup + scene-style output naming)
vmavificient encode movie.mkv --tmdb 603

# Skip TMDB; output is <stem>.av1.mkv
vmavificient encode movie.mkv --blind

# Specific preset (one of: live-action [default], animation,
# super35_analog, super35_digital, imax_analog, imax_digital)
vmavificient encode movie.mkv --animation --target-vmaf 95

# Dual-output: UHD + 1920p companion in one invocation
vmavificient encode uhd.mkv --companion-hd

# Plan without writing anything to disk
vmavificient encode movie.mkv --dry-run

# Resume an interrupted encode (state.json carries the per-step status)
vmavificient encode movie.mkv --cache-dir /path/to/cache
```

See `vmavificient encode --help` for the full flag list (preset
shorthand, source/language overrides, VBR mode, `--scale-to-hd`,
`--quiet`/`--verbose`, `--srt`, etc.).

## Build from source

Requires Clang/LLVM 20+, CMake 3.24+, Ninja, and Git submodules.

```sh
git clone --recursive https://github.com/matherose/VMAVIFICIENT.git
cd VMAVIFICIENT
cmake --preset linux-x86_64           # or linux-aarch64, macos-arm64,
                                      # windows-x86_64-mingw
cmake --build --preset linux-x86_64
ctest --test-dir build/linux-x86_64 --output-on-failure
```

CMake options:
- `-DVMAV_FETCH_REAL_CONTENT=ON` — fetch the Big Buck Bunny + synthetic
  HDR10 fixtures for the real-content integration tests. OFF by default
  (clean clones without network still pass the 43 unit + 3 synthetic
  integration tests).
- `-DVMAV_BUNDLE_TESSDATA=ON` — fetch eng.traineddata and install it
  alongside the binary. ON for packaging builds.
- `-DVMAV_BUILD_TESTS=ON` (default) — opt out with `=OFF` to skip the
  test binaries entirely.

## CI / CD

Single unified pipeline (`.github/workflows/pipeline.yml`), 5 stages:

```
                            lint  build  test  package  publish
push to feature branch       ✓     ✓     ✓       -        -
pull_request to main         ✓     ✓     ✓       ✓        -
push to main                 ✓     ✓     ✓       ✓        -
push tag refs/tags/v*        ✓     ✓     ✓       ✓        ✓
```

Reproducibility: every binary is built with `SOURCE_DATE_EPOCH` +
prefix-maps + `--build-id=none`. Main + tag refs additionally PROVE
it via `scripts/repro_check.sh` (double-build sha256 compare).

## License

Source code: **WTFPL v2** — see [`LICENSE`](LICENSE).
Pre-built binaries: **GPL-3.0-or-later** (inherited from FFmpeg
`--enable-gpl`) — see [`LICENSE-BINARY`](LICENSE-BINARY) and
[`docs/licensing.md`](docs/licensing.md).
