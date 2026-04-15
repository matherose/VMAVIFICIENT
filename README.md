# VMAVIFICIENT

A self-contained AV1 encoding toolkit for high-quality HDR media archival. Takes a source file (Blu-ray REMUX, WEB-DL, etc.) and produces a fully muxed MKV with AV1 video, Opus audio, and all metadata preserved — Dolby Vision, HDR10+, HDR10, subtitles, and chapter markers.

Built for the scene-release workflow: automated naming, language tagging, crop detection, grain analysis, and TMDB integration.

## Features

- **SVT-AV1-HDR encoding** — Uses the [svt-av1-hdr](https://github.com/juliobbv-p/svt-av1-hdr) fork (v4.0.1) with full Dolby Vision RPU passthrough and HDR10+ dynamic metadata
- **VMAF-driven CRF search** — Cuts sample clips, encodes at adaptively chosen CRF values using interpolated binary search, and converges on the lowest CRF that meets a target VMAF p10 (default 93)
- **XPSNR sanity-check** — Reports XPSNR p10 (dB) on the chosen probe as informational cross-metric output
- **Film grain synthesis** — Analyzes source grain characteristics and applies matching SVT-AV1 grain synthesis for transparent encoding of grainy content
- **Dolby Vision & HDR10+** — RPU extraction via [libdovi](https://github.com/quietvoid/dovi_tool), HDR10+ metadata via [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool), full passthrough to AV1
- **Opus audio** — Full VBR at 56 kbps/channel, compression level 10, multichannel mapping for 5.1/7.1
- **Automatic crop detection** — Multi-frame cropdetect sampling (letterbox, pillarbox)
- **OCR subtitle extraction** — PGS bitmap subtitles to SRT via Tesseract
- **Scene-release naming** — TMDB lookup, language tagging (MULTi, VFF, VFQ, etc.), source detection, automated output naming

## Architecture

```
Source MKV/REMUX
       |
       v
  [media_info]     Validate, extract resolution/fps/duration
  [media_hdr]      Detect DV profile, HDR10+, PQ transfer
  [media_crop]     Cropdetect via FFmpeg filter
  [media_analysis] Grain score analysis
       |
       v
  [crf_search]     Cut samples -> adaptive CRF probes (interpolated bisection)
       |            -> score with VMAF (p10 metric) + XPSNR (informational)
       |            -> derive optimal VBR bitrate at target VMAF
       v
  [video_encode]   SVT-AV1-HDR encode (VBR, preset 4, 10-bit)
       |            + DV RPU injection per-frame
       |            + film grain synthesis
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
| [FFmpeg](https://ffmpeg.org/) | n8.1 | Decode/mux/filter |
| [SVT-AV1-HDR](https://github.com/juliobbv-p/svt-av1-hdr) | v4.0.1 | AV1 encoder with DV/HDR10+ |
| [libdovi](https://github.com/quietvoid/dovi_tool) | 3.3.2 | Dolby Vision RPU parsing |
| [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool) | 2.1.5 | HDR10+ metadata |
| [Opus](https://opus-codec.org/) | 1.5.2 | Audio codec |
| [cJSON](https://github.com/DaveGamble/cJSON) | 1.7.18 | JSON parsing (TMDB / VMAF) |

Perceptual scoring uses FFmpeg's built-in `libvmaf` and `xpsnr` filters (bundled in the FFmpeg build).

System requirements:

- **Rust** toolchain (for libdovi / libhdr10plus via cargo-c)
- **Tesseract** + **Leptonica** (for OCR subtitle extraction)
- **pkg-config**, **CMake** >= 3.20, **Ninja**
- **curl** (for TMDB API)
- macOS or Linux (tested on macOS with Apple Silicon)

## Building

```bash
cmake -G Ninja -B build
cmake --build build -j$(nproc)
```

The build fetches and compiles all dependencies automatically. First build takes a while (FFmpeg, SVT-AV1, Rust crates); subsequent builds are incremental.

## Usage

```bash
# Basic usage — auto-detects everything
./build/vmavificient input.mkv

# With TMDB naming
./build/vmavificient --tmdb 335984 input.mkv

# Override bitrate (skips CRF search)
./build/vmavificient --bitrate 2500 input.mkv

# Quality preset for specific content types
./build/vmavificient --imax_analog input.mkv
./build/vmavificient --animation input.mkv
```

### Configuration

Copy `config.ini.example` to `config.ini` and fill in your values:

```ini
tmdb_api_key = <your TMDB v3 API key>
release_group = MyGroup
```

### Quality presets

| Preset | Flag | Optimized for |
|---|---|---|
| Live-action | *(default)* | Standard cinema content |
| Animation | `--animation` | Anime and animated films |
| Super 35 Analog | `--super35_analog` | 35mm film grain |
| Super 35 Digital | `--super35_digital` | Digital cinema cameras |
| IMAX Analog | `--imax_analog` | IMAX film (65mm/70mm) |
| IMAX Digital | `--imax_digital` | IMAX digital cameras |

## How the bitrate search works

When no `--bitrate` is specified, VMAVIFICIENT runs an automatic quality calibration:

1. **Sample** — Cuts 2 x 10s clips from the source at different points in the timeline
2. **Bracket** — Encodes at CRF 30, then CRF 45 or 20 depending on score, to bracket the target VMAF p10
3. **Bisect** — Linearly interpolates the next probe CRF from the current bracket, encodes, and tightens the bracket — stops when width <= 2 CRF units or probe budget is hit
4. **Report** — Emits the recommended CRF + predicted VBR bitrate, plus an informational XPSNR p10 reading on the chosen probe for cross-metric sanity
5. **Encode** — Runs the final full encode at the derived bitrate

VMAF is the best-correlated full-reference metric for AV1 at 4K ([arxiv 2511.00969](https://arxiv.org/html/2511.00969) measures PCC 0.902 vs subjective MOS). For HDR/PQ sources, VMAF operates on the luma plane which preserves its correlation with perceived quality in practice.

## License

This project is licensed under the [GNU Lesser General Public License v3.0](LICENSE) (LGPL-3.0-or-later).

This means you can use, modify, and distribute this software freely, including in proprietary projects, as long as modifications to VMAVIFICIENT itself remain under LGPL-3.0. See [LICENSE](LICENSE) for the full text.

### Third-party licenses

- [FFmpeg](https://ffmpeg.org/): LGPL-2.1+ / GPL-2.0+ (built with `--enable-gpl`)
- [SVT-AV1-HDR](https://github.com/juliobbv-p/svt-av1-hdr): BSD-3-Clause
- [libdovi](https://github.com/quietvoid/dovi_tool): MIT
- [libhdr10plus](https://github.com/quietvoid/hdr10plus_tool): MIT
- [Opus](https://opus-codec.org/): BSD-3-Clause
- [cJSON](https://github.com/DaveGamble/cJSON): MIT

## Credits

Built with significant help from [Claude](https://claude.ai) (Anthropic) — VMAF CRF search orchestrator and encoding pipeline.
