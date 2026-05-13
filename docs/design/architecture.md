# Architecture Overview

## System Overview

VMAVIFICIENT is a video encoding utility that processes video files and produces AV1-encoded MKV containers with:

- Video encoding using SVT-AV1
- Audio encoding with Opus
- HDR metadata extraction and processing (Dolby Vision RPU, HDR10+)
- Subtitle format conversion
- Metadata lookup from TMDB
- Configurable encoding presets and rate control

## high-level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          VMAVIFICIENT (Main)                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  cli/         Command-line interface and argument parsing                   │
│  main.c       Entry point, orchestrates pipeline                            │
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                         Encoding Pipeline                           │    │
│  ├─────────────────────────────────────────────────────────────────────┤    │
│  │ media_analysis.c   Analyze input video/audio streams                │    │
│  │ media_crop.c       Crop/padding calculations                        │    │
│  │ media_hdr.c        Extract HDR metadata (RPU, HDR10+)               │    │
│  │ video_encode.c     SVT-AV1 video encoding                           │    │
│  │ audio_encode.c     Opus audio encoding                              │    │
│  │ subtitle_convert.c Subtitle format conversion                       │    │
│  │ rpu_extract.c      Dolby Vision RPU extraction                      │    │
│  │ crf_search.c       Constant rate factor search                      │    │
│  │ final_mux.c        MKV muxing and final assembly                    │    │
│  │ tmdb.c             Metadata lookup from TMDB API                    │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                               │
│  config.c     Configuration parsing (config.ini)                            │
│  utils.c      Common utility functions                                      │
│  ui.c         UI progress reporting                                         │
│                                                                               │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Component Details

### Media Analysis (`media_analysis.c`)

- Parses input files using libavformat/libavcodec
- Extracts stream information ( resolution, FPS, bit depth, color space)
- Detects HDR metadata presence

### HDR Processing (`media_hdr.c`, `rpu_extract.c`)

- Extracts Dolby Vision RPU from video stream
- Processes HDR10+ dynamic metadata
- Passes metadata to encoder configuration

### Video Encoding (`video_encode.c`)

- Wraps SVT-AV1 encoder library
- Implements CRF/VBR encoding modes
- Handles GOP structure and rate control

### Audio Encoding (`audio_encode.c`)

- Wraps libopus encoder
- Handles channel layout and quality settings
- Embeds audio in MKV container

### Muxing (`final_mux.c`)

- Combines video, audio, and subtitle tracks
- Adds metadata tags
- Finalizes MKV container

## Build Architecture

```
CMakeLists.txt (root)
├── src/               Module source files
│   ├── audio_encode/
│   ├── config/
│   ├── crf_search/
│   ├── encode_preset/
│   ├── final_mux/
│   ├── media_analysis/
│   ├── media_crop/
│   ├── media_hdr/
│   ├── media_info/
│   ├── media_naming/
│   ├── media_tracks/
│   ├── rpu_extract/
│   ├── subtitle_convert/
│   ├── tmdb/
│   ├── ui/
│   ├── utils/
│   └── video_encode/
├── include/vmavificient/  Public headers
├── cmake/               CMake helper modules
│   ├── CompilerFlags.cmake
│   ├── Sanitizers.cmake
│   ├── CodeCoverage.cmake
│   └── Criterion.cmake
├── tests/unit/          Unit tests
├── fuzz/                libFuzzer targets
├── scripts/             Development scripts
└── docs/                Documentation
```

## Toolchain

- **Compiler:** LLVM/Clang 15+
- **Standard:** C11 (strict, pedantic)
- **Build:** CMake 3.25+, Ninja
- **Testing:** Criterion + CTest
- **Static Analysis:** clang-tidy, scan-build

## Quality Gates

1. **Format check:** clang-format (zero warnings)
2. **Static analysis:** clang-tidy (zero findings)
3. **Compile:** debug preset (zero warnings)
4. **Tests:** debug + asan builds
5. **Additional sanitizers:** msan, tsan
6. **Coverage:** 80% line minimum
7. **Documentation:** Doxygen zero warnings

See `.github/workflows/ci.yml` for CI implementation.

## Dependencies

- **Runtime:** FFmpeg, SVT-AV1, libopus, libdovi, libhdr10plus, tesseract, libjpeg, libpng, libtiff, cjson
- **Build:** CMake, Ninja, Clang, pkg-config
- **Test:** Criterion, CTest

See `CMakeLists.txt` for full dependency specification.
