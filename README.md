# VMAVIFICIENT

A self-contained AV1 encoding toolkit for high-quality HDR media archival, targeting "HDLight" / "4KLight" release tiers. Takes a source file (Blu-ray REMUX, WEB-DL, etc.) and produces a fully muxed MKV with AV1 video, Opus audio, and all metadata preserved — Dolby Vision, HDR10+, HDR10, subtitles, and chapter markers.

Built for the scene-release workflow: automated naming, language tagging, crop detection, grain analysis, and TMDB integration.

## Features

- **SVT-AV1-HDR encoding** — Uses the [svt-av1-hdr](https://github.com/juliobbv-p/svt-av1-hdr) fork (v4.1.0 "Chromedome") with full Dolby Vision RPU passthrough and HDR10+ dynamic metadata
- **Flat tier-based bitrate** — No perceptual search; bitrates are pinned by tier × resolution × content type (see table below). Predictable wall-time, predictable output sizes.
- **Dual grain mechanism** — `--noise` (synthetic overlay, zero overhead) for digital sources + animation; `--film-grain` (analyzed + denoised) for analog film sources where source grain has structure worth preserving
- **Dolby Vision & HDR10+** — RPU extraction via [libdovi](https://github.com/quietvoid/dovi_tool), HDR10+ metadata via [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool), full passthrough to AV1
- **Opus audio** — Full VBR at 56 kbps/channel, compression level 10, multichannel mapping for 5.1 / 7.1
- **Automatic crop detection** — Multi-frame cropdetect sampling (letterbox, pillarbox)
- **OCR subtitle extraction** — PGS bitmap subtitles to SRT via Tesseract
- **Scene-release naming** — TMDB lookup, language tagging (MULTi, VFF, VFQ, etc.), source detection, automated output naming

## Bitrate tiers

| Resolution | Content | Bitrate (kbps) |
|---|---|---|
| **4K** (≥ 2160p) | Live-action grainy | 4000 |
| | Live-action clean | 3500 |
| | Animation | 3000 |
| **HD** (< 2160p) | Live-action grainy | 2500 |
| | Live-action clean | 2000 |
| | Animation | 1500 |

The "grainy" branch fires when the measured `grain_score >= 0.08`. Animation always uses the clean tier minus 500 kbps (no real grain to budget for). Override with `--bitrate <kbps>` if you want a specific value.

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
--blind          Skip TMDB; name output as <input-stem>.mkv
--bitrate <kbps> Override target video bitrate (skips tier table)
--srt <path>     Additional SRT subtitle file (can be repeated)

--dry-run        Run analysis + naming, print encoding plan, exit. No files written.
--grain-only     Like --dry-run, plus dump every encoder knob the resolved
                 preset configures (grain mech, tune, ac-bias, filters, QMs).
                 For sanity-checking what each tier actually does.
--quiet          Compact output — only stage status lines + Plan + Done blocks
--verbose        Forward SVT-AV1 encoder log messages to stderr (rate control,
                 GOP layout, warnings). Composes with --quiet.

--help           Show the full flag list (incl. all language + source tags)
```

Honors `NO_COLOR=1` to disable ANSI colors. Honors `VMAV_KEEP_GRAIN_TMP=1` to retain per-window grain analysis scratch files for inspection.

## Architecture

```
Source MKV/REMUX
       |
       v
  [media_info]     Validate, extract resolution/fps/duration
  [media_hdr]      Detect DV profile, HDR10+, PQ transfer
  [media_crop]     Cropdetect via FFmpeg filter
  [media_analysis] Grain score analysis (per-window via grav1synth diff)
       |
       v
  [encode_preset]  Resolve preset by quality type + resolution
                   Compute target bitrate from tier × grain_score × content
                   Compute grain synth level from grain_score
       |
       v
  [video_encode]   SVT-AV1-HDR 4.1.0 encode (VBR, preset 4, 10-bit)
       |            + DV RPU injection per-frame
       |            + grain mechanism (--noise or --film-grain)
       v
  [audio_encode]   Opus encode per audio track
  [subtitle_conv]  PGS -> SRT via Tesseract OCR
  [rpu_extract]    DV RPU extraction for AV1 injection
       |
       v
  [final_mux]      FFmpeg remux to final MKV
  [media_naming]   TMDB + scene naming conventions
```

## Dependencies

All major dependencies are built from source via CMake `ExternalProject`:

| Dependency | Version | Purpose |
|---|---|---|
| [FFmpeg](https://ffmpeg.org/) | n8.1 | Decode / mux / filter |
| [SVT-AV1-HDR](https://github.com/juliobbv-p/svt-av1-hdr) | v4.1.0 | AV1 encoder with DV / HDR10+ + `--noise` |
| [libdovi](https://github.com/quietvoid/dovi_tool) | 3.3.2 | Dolby Vision RPU parsing |
| [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool) | 2.1.5 | HDR10+ metadata |
| [Opus](https://opus-codec.org/) | 1.5.2 | Audio codec |
| [grav1synth](https://github.com/rust-av/grav1synth) | (pinned SHA) | Grain measurement |
| [Tesseract](https://github.com/tesseract-ocr/tesseract) | 5.5.0 | OCR for PGS subs |
| [cJSON](https://github.com/DaveGamble/cJSON) | 1.7.18 | TMDB JSON parsing |

System requirements:

- **Rust** toolchain (for libdovi / libhdr10plus via cargo-c)
- **pkg-config**, **CMake** ≥ 3.24, **Ninja**
- **curl** (for TMDB API)
- **LLVM/Clang** (the build forces it; gcc is not supported)
- macOS or Linux (tested on macOS with Apple Silicon)

## Building

```bash
cmake -G Ninja -B build
cmake --build build -j$(nproc)
```

The build fetches and compiles all dependencies automatically. First build takes a while (FFmpeg + SVT-AV1 + Rust crates ≈ 15–30 min); subsequent builds are incremental.

## Usage

```bash
# Basic — auto-detects everything, names from TMDB
./build/vmavificient --tmdb 335984 input.mkv

# Blind mode — keep the input filename, skip TMDB lookup
./build/vmavificient --blind input.mkv

# Sanity-check the plan without encoding
./build/vmavificient --blind --dry-run input.mkv

# Same, but also dump every encoder knob the preset configures
./build/vmavificient --blind --grain-only input.mkv

# Override bitrate
./build/vmavificient --blind --bitrate 5000 input.mkv

# Animation content (uses 1500/3000 kbps tier)
./build/vmavificient --animation --blind input.mkv

# 35mm film source (uses tune 5 + --film-grain)
./build/vmavificient --super35_analog --blind input.mkv

# Compact output for batch / overnight runs
./build/vmavificient --quiet --blind input.mkv

# Forward SVT-AV1 chatter to stderr for debugging
./build/vmavificient --verbose --blind input.mkv
```

### Configuration

Copy `config.ini.example` to `config.ini` and fill in your values:

```ini
tmdb_api_key = <your TMDB v3 API key>
release_group = MyGroup
```

`config.ini` is only required when using `--tmdb`. With `--blind` the tool works without any config.

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
