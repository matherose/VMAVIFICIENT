# VMAVIFICIENT

A self-contained AV1 encoding toolkit for high-quality HDR media archival, targeting "HDLight" / "4KLight" release tiers. Takes a source file (Blu-ray REMUX, WEB-DL, etc.) and produces a fully muxed MKV with AV1 video, Opus audio, and all metadata preserved — Dolby Vision, HDR10+, HDR10, subtitles, and chapter markers.

Built for the scene-release workflow: automated naming, language tagging, crop detection, grain analysis, and TMDB integration.

## Features

- **SVT-AV1-HDR encoding** — Uses the [svt-av1-hdr](https://github.com/juliobbv-p/svt-av1-hdr) fork (v4.1.0 "Chromedome") with full Dolby Vision RPU passthrough and HDR10+ dynamic metadata
- **In-process CRF search** — Binary-searches CRF to hit a per-preset VMAF target (roughly 90–96 depending on preset) instead of pinning a flat bitrate; `--bitrate` can skip the search for a flat VBR encode, `--crf` can pin the CRF directly
- **Dual grain mechanism** — `--noise` (synthetic overlay, zero overhead) for digital sources + animation; `--film-grain` (analyzed + denoised) for analog film sources where source grain has structure worth preserving
- **Dolby Vision & HDR10+** — RPU extraction via [libdovi](https://github.com/quietvoid/dovi_tool), HDR10+ metadata via [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool), full passthrough to AV1
- **Opus audio** — Full VBR at 56 kbps/channel, compression level 10, multichannel mapping for 5.1 / 7.1
- **Automatic crop detection** — Multi-frame cropdetect sampling (letterbox, pillarbox)
- **OCR subtitle extraction** — PGS bitmap subtitles to SRT via Tesseract
- **Scene-release naming** — TMDB lookup, language tagging (MULTi, VFF, VFQ, etc.), source detection, automated output naming

## Rate control

By default, vmavificient runs an in-process CRF search that binary-searches
CRF until the encode's measured VMAF hits the per-preset target (roughly
90–96 depending on preset). This replaces perceptual guesswork with a direct
quality target, at the cost of a probe encode or two before the final pass.

- `--bitrate <kbps>` skips the CRF search entirely and encodes VBR at a flat
  bitrate you choose.
- `--crf <N>` skips the search and pins the CRF directly (1–63, lower =
  higher quality).
- `--vmaf-target <N>` overrides the per-preset VMAF target used by the CRF
  search (1–100).

## Grain handling

The encoder uses one of two AV1 grain mechanisms depending on the preset:

| Preset | Mechanism | Why |
|---|---|---|
| Live-action (default) | `--noise` | Digital cameras → sensor noise, not grain |
| Animation | `--noise` (low) | Light overlay just to mask banding |
| Super 35 Digital, IMAX Digital | `--noise` | Digital — no real grain |
| Super 35 Analog, IMAX Analog | `--film-grain` + denoise | Real film grain has structure worth preserving |

Differences:

| | `--film-grain N` | `--noise N` |
|---|---|---|
| What it does | Analyzes source noise → fgs_table | Generates synthetic noise table mathematically |
| Source modification | Optional denoise | None |
| Encoding cost | Denoise step + analysis | Zero |
| Look | Source grain character | Generic camera noise |

`--noise` came in SVT-AV1-HDR 4.1.0 ("Chromedome"). For our HDLight bitrate target it's a much better fit on digital sources than `--film-grain` because there's no source detail loss from the denoising step.

## Quality presets

| Preset | Flag | Optimized for | Grain mech |
|---|---|---|---|
| Live-action | *(default)* | Standard cinema content | `--noise` |
| Animation | `--animation` | Anime / animated films | `--noise` (low) |
| Super 35 Analog | `--super35_analog` | 35mm film stock | `--film-grain` |
| Super 35 Digital | `--super35_digital` | Digital cinema cameras | `--noise` |
| IMAX Analog | `--imax_analog` | IMAX film (65mm/70mm) | `--film-grain` |
| IMAX Digital | `--imax_digital` | IMAX digital cameras | `--noise` |

## CLI flags

```
--tmdb <id>      TMDB movie ID for naming (requires TMDB_API_KEY)
--tv             TV mode: --tmdb <id> is a TMDB series ID (themoviedb.org/tv/<id>).
                 Output is named Show.SxxEyy.Episode.Title.<...> — no year.
--mv             Movie mode (the default; explicit form)
--season <N>     Season number (with --tv; overrides filename parsing,
                 prompts if still unknown)
--episode <N>    Episode number (with --tv; same resolution order as --season)
--blind          Skip TMDB lookup; name output as <input-stem>.mkv
                 (no config required)
--config         Run interactive setup once; writes
                 $HOME/.config/vmavificient/config.ini with the TMDB API key
                 and release group. Subsequent runs read it automatically.
--crf <N>        Skip CRF search; encode at this CRF directly
                 (1–63, lower = higher quality)
--vmaf-target <N> Override the VMAF target for CRF search
                 (default: per-preset, 90–96)
--bitrate <kbps> Skip CRF search; encode VBR at this bitrate
--srt <path>     Additional SRT subtitle file (can be repeated)

--dry-run        Run analysis + CRF search + naming, print the encoding plan,
                 then exit. No files written.
--quiet          Compact output: hide informational sections, keep only
                 stage status lines + the Plan / Done blocks.
--verbose        Forward SVT-AV1 encoder log messages to stderr (rate control,
                 GOP layout, warnings). Composes with --quiet.
--grain-only     Like --dry-run, plus dump every encoder knob the resolved
                 preset configures (grain mech, tune, ac-bias, filters, QMs).
                 For sanity-checking what each tier actually does without a
                 full encode.
--companion-hd   After the 4K encode, produce a second 1080p HDLight release
                 from the same REMUX source. Requires a 4K source. Audio and
                 subtitles are shared between both outputs. Dolby Vision is
                 stripped from the HD output.
--scale-to-hd    Produce only a 1080p HDLight release (no 4K output).
                 Requires a 4K source. Full independent pipeline. Mutually
                 exclusive with --companion-hd.
--cache-dir <path>
                 Use specified directory for intermediate files (grain
                 analysis, CRF search results, extracted audio/subtitles).
                 Cache is deleted after successful encode. Defaults to a
                 hidden .vmavificient-cache folder in the project root.

--help           Show the full flag list (incl. all language + source tags)
```

Running with no input file (and no `--help`/`--config`) prints this usage text and exits 1.

Honors `NO_COLOR=1` to disable ANSI colors. Honors `VMAV_KEEP_GRAIN_TMP=1` to retain per-window grain analysis scratch files for inspection.

## Architecture

The `pipeline` module drives the stages below in order, with resumable state
persisted to `<cache-dir>/state.json` so an interrupted run can pick back up
without redoing finished stages.

```
Source MKV/REMUX
       |
       v
  [media_info]     Validate, extract resolution/fps/duration
  [media_hdr]      Detect DV profile, HDR10+, PQ transfer
       |
       v
  [media_crop]     Crop detect — cropdetect via FFmpeg filter
       |
       v
  [media_analysis] Grain analysis — per-window grain score via grav1synth diff
  [encode_preset]  Resolve preset by quality type + resolution
                   Compute grain synth level from grain_score
       |
       v
  [crf_search]     CRF search — binary-search CRF against the per-preset
                   VMAF target (skipped by --bitrate or --crf)
       |
       v
  [audio_encode]   Audio encode — Opus per audio track
       |
       v
  [subtitle_conv]  Subtitles — PGS -> SRT via Tesseract OCR
  [rpu_extract]    DV RPU extraction for AV1 injection
       |
       v
  [video_encode]   Video encode — SVT-AV1-HDR 4.1.0 (preset 4, 10-bit)
       |            + DV RPU injection per-frame
       |            + grain mechanism (--noise or --film-grain)
       v
  [final_mux]      Mux — FFmpeg remux to final MKV
  [media_naming]   TMDB + scene naming conventions
```

`tmdb` (naming/lookup) and `pipeline` (stage orchestration, state.json
resume) sit alongside these stages rather than in the linear flow above.

## Dependencies

Since v1.1.0 the build uses system-provided shared libraries via pkg-config by default. Three deps still build from source because they aren't in any distro repo:

| Dependency | Version | Source | Why vendored |
|---|---|---|---|
| [SVT-AV1-HDR](https://github.com/juliobbv-p/svt-av1-hdr) | v4.1.0 | `ExternalProject_Add` | Need the HDR fork (`--noise`, DV/HDR10+ passthrough); Debian only ships upstream SVT-AV1 |
| [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool) | 2.1.5 | `cargo cinstall` | Not packaged in Debian; Homebrew formula installs the binary only |
| [grav1synth](https://github.com/rust-av/grav1synth) | (pinned SHA) | `cargo install` | CLI tool, invoked as a subprocess for grain measurement |

Everything else comes from the system: FFmpeg (avformat/avcodec/avutil/avfilter/swscale/swresample), Opus, libdovi, Tesseract+Leptonica, libpng/jpeg/tiff, zlib, OpenSSL, libcurl, cJSON.

### System requirements

- **macOS arm64** (Apple Silicon). Linux support is on the roadmap but
  deferred — `libdovi-dev` only landed in Debian trixie / Ubuntu 25.04+,
  and we don't want to ship a half-working Linux story.
- **CMake** ≥ 3.24, **Ninja**, **pkg-config**
- **LLVM/Clang** (the build forces it; gcc is not supported)
- **Rust** toolchain (for libhdr10plus / grav1synth)
- **cargo-c** (`cargo install cargo-c`) for libhdr10plus

### Install build + runtime deps (macOS)

```bash
brew install \
    ninja pkg-config nasm rust cargo-c meson \
    ffmpeg opus dovi_tool tesseract leptonica libvmaf \
    jpeg-turbo libpng libtiff cjson openssl@3
```

## Building

```bash
cmake -G Ninja -B build
cmake --build build -j$(nproc)
```

First build takes ~3 minutes (just SVT-AV1-HDR + libhdr10plus + grav1synth from source). Subsequent builds are incremental.

### Static / vendored build

For a fully static binary that depends on nothing at runtime — used for the GitHub release artifact — pass `-DVMAV_USE_SYSTEM_DEPS=OFF`:

```bash
cmake -G Ninja -B build -DVMAV_USE_SYSTEM_DEPS=OFF
cmake --build build -j$(nproc)
```

This vendors and statically links every dependency. First build takes 15–30 minutes (FFmpeg + Tesseract + OpenSSL + the Rust crates from source). Use this only when you want a portable binary; otherwise the default system-deps mode is faster and produces a much smaller binary (~4 MB vs ~38 MB).

### Sanitizer build

```bash
cmake -G Ninja -B build-asan -DVMAV_SANITIZE=ON
cmake --build build-asan -j$(nproc)
```

Adds `-fsanitize=address,undefined` for catching memory / UB bugs locally. Used by CI on every push.

## Usage

```bash
# Basic — auto-detects everything, names from TMDB
./build/vmavificient --tmdb 335984 input.mkv

# TV mode example (TMDB series ID, manual season/episode)
./build/vmavificient --tv --tmdb 890 "Neon Genesis Evangelion - Episode 01.mkv" --season 1 --episode 1

# Blind mode — keep the input filename, skip TMDB lookup
./build/vmavificient --blind input.mkv

# Sanity-check the plan without encoding
./build/vmavificient --blind --dry-run input.mkv

# Same, but also dump every encoder knob the preset configures
./build/vmavificient --blind --grain-only input.mkv

# Skip the CRF search, encode VBR at a flat bitrate
./build/vmavificient --blind --bitrate 5000 input.mkv

# Skip the CRF search, pin CRF directly
./build/vmavificient --blind --crf 24 input.mkv

# Animation content
./build/vmavificient --animation --blind input.mkv

# 35mm film source (uses tune 5 + --film-grain)
./build/vmavificient --super35_analog --blind input.mkv

# Compact output for batch / overnight runs
./build/vmavificient --quiet --blind input.mkv

# Forward SVT-AV1 chatter to stderr for debugging
./build/vmavificient --verbose --blind input.mkv
```

### Configuration

Run the one-shot interactive setup; it prompts for the TMDB API key and the release group, then writes `$HOME/.config/vmavificient/config.ini` with `0600` permissions:

```bash
vmavificient --config
```

Subsequent invocations read it automatically — `vmavificient --tmdb 335984 input.mkv` "just works" after that.

If you'd rather edit the file by hand, the format is two `key = value` lines:

```ini
tmdb_api_key = <your TMDB v3 API key>
release_group = MyGroup
```

For dev work from the build tree, vmavificient also accepts `./config.ini` next to the binary as a fallback. With `--blind` the tool works without any config at all.

### Homebrew (macOS)

```bash
brew tap matherose/vmavificient
brew install vmavificient
vmavificient --config
```

## License

LGPL-3.0-or-later. See [LICENSE](LICENSE).

### Third-party licenses

- [FFmpeg](https://ffmpeg.org/): LGPL-2.1+ / GPL-2.0+ (built with `--enable-gpl`)
- [SVT-AV1-HDR](https://github.com/juliobbv-p/svt-av1-hdr): BSD-3-Clause
- [libdovi](https://github.com/quietvoid/dovi_tool): MIT
- [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool): MIT
- [Opus](https://opus-codec.org/): BSD-3-Clause
- [grav1synth](https://github.com/rust-av/grav1synth): MIT
- [Tesseract](https://github.com/tesseract-ocr/tesseract): Apache-2.0
- [cJSON](https://github.com/DaveGamble/cJSON): MIT

## Credits

Built with significant help from [Claude](https://claude.ai) (Anthropic).
