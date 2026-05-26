#!/usr/bin/env bash
# fetch_real_content.sh — populate a deterministic local cache with real
# Creative Commons video clips used by the Phase 8 integration tests.
#
# Two fixtures are produced under the cache dir:
#
#   bbb_clip.mkv            5s slice of Big Buck Bunny (1280x720 H.264 +
#                           AAC, animation content, exercises the SDR
#                           pipeline including audio).
#
#   tos_hdr_clip.mkv        5s slice of Tears of Steel HDR (UHD HDR10
#                           HEVC + AAC, exercises the HDR-passthrough
#                           path including mastering display / CLL).
#
# Both fixtures live under <cache>/<name> where <cache> defaults to
# tests/fixtures/.real-content-cache/ (relative to the repo root if no
# --cache-dir is given). The upstream sources are downloaded once and
# sha256-validated; the trimmed outputs are written deterministically
# (re-running the script is a no-op if the trimmed outputs exist).
#
# Usage:
#   scripts/fetch_real_content.sh [--cache-dir <path>] [--force]
#
# Options:
#   --cache-dir <path>   Where to materialize sources + clips
#                        (default: <repo>/tests/fixtures/.real-content-cache)
#   --force              Re-download + re-trim even if outputs exist.
#
# CI integration: the cache dir is keyed by this script's sha256 + the
# upstream URL list, so any change to the script or sources invalidates
# the cache and triggers a fresh fetch.

set -euo pipefail

CACHE_DIR=""
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --cache-dir) CACHE_DIR="$2"; shift 2;;
        --force) FORCE=1; shift;;
        -h|--help)
            sed -n '2,28p' "$0"; exit 0;;
        *)
            echo "fetch_real_content.sh: unknown arg $1" >&2; exit 2;;
    esac
done

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "$CACHE_DIR" ]]; then
    CACHE_DIR="$repo_root/tests/fixtures/.real-content-cache"
fi
mkdir -p "$CACHE_DIR"

log() { printf '[fetch_real_content] %s\n' "$*"; }

require() {
    if ! command -v "$1" > /dev/null 2>&1; then
        echo "fetch_real_content.sh: missing required tool: $1" >&2
        exit 2
    fi
}
require curl
require ffmpeg
require shasum

# sha256 helper (portable: shasum -a 256 works on macOS + Linux).
sha256() { shasum -a 256 "$1" | awk '{print $1}'; }

# Download $1 → $2 if missing or if --force, then verify sha256 == $3.
# Aborts on hash mismatch.
fetch_with_sha() {
    local url="$1" out="$2" expected="$3"
    if [[ $FORCE -eq 1 || ! -f "$out" ]]; then
        log "downloading $url"
        curl --fail --location --silent --show-error --output "$out.tmp" "$url"
        mv "$out.tmp" "$out"
    fi
    local got
    got="$(sha256 "$out")"
    if [[ "$got" != "$expected" ]]; then
        echo "fetch_real_content.sh: sha256 mismatch for $out" >&2
        echo "  expected $expected" >&2
        echo "  got      $got" >&2
        rm -f "$out"
        exit 1
    fi
}

# ============================================================
# Big Buck Bunny — official Blender Foundation trailer mirror.
# 30 MB, 1080p QuickTime, ~1 minute duration. We trim to a 5s
# slice for the integration tests. The Blender CDN has been
# stable since 2008.
# ============================================================
BBB_URL="https://download.blender.org/peach/trailer/trailer_1080p.mov"
BBB_SHA="67a3abdfd142e6c32a1994960077a5669b59a5745adceac41de8de540736dc9f"
BBB_SOURCE="$CACHE_DIR/bbb_source.mov"
BBB_CLIP="$CACHE_DIR/bbb_clip.mkv"

if [[ $FORCE -eq 1 || ! -f "$BBB_CLIP" ]]; then
    fetch_with_sha "$BBB_URL" "$BBB_SOURCE" "$BBB_SHA"
    log "trimming BBB source → $BBB_CLIP (5s slice from t=4s)"
    # -ss BEFORE -i = fast seek (decoder skips packets up to keyframe
    # near 4s, then we re-encode the trim so the output starts on a
    # keyframe — cleaner for downstream demuxers). We re-encode video
    # too (libx264) so the slice is self-contained instead of
    # depending on referenced frames preceding the trim point.
    ffmpeg -y -hide_banner -loglevel error \
        -ss 4 -i "$BBB_SOURCE" -t 5 \
        -map 0:v:0 -map 0:a:0? \
        -c:v libx264 -preset ultrafast -crf 18 \
        -c:a aac -b:a 128k \
        "$BBB_CLIP"
    log "BBB clip ready: $(du -h "$BBB_CLIP" | awk '{print $1}')"
else
    log "BBB clip already present: $BBB_CLIP"
fi

# ============================================================
# HDR10 fixture — synthetic 3840x2160 HEVC test pattern with
# real SMPTE ST 2086 + CTA-861.3 metadata in SEI. We can't pin
# a small downloadable Creative Commons HDR clip (Blender's
# Tears of Steel HDR distribution is multi-variant and the
# 1080p mov is 583 MB SDR-only; xiph 4K HDR samples are >6 GB
# Y4M). Synthesizing via libx265 lets us assert a true HDR10
# round-trip on a fixture small enough for CI.
#
# Mastering display: Display P3 primaries (Apple/Pro Display
# XDR family) — matches the dominant grading target for HDR
# Blu-ray. min/max luminance: 0.005 / 4000 cd/m^2 (4000-nit
# mastering monitor). MaxCLL 1000 / MaxFALL 400 — typical
# values for HDR streaming content.
#
# Same scales the AV1 spec defines (x50000 for chromaticity,
# x10000 for luminance), passed straight to x265's
# --master-display + --max-cll. The vmav HDR-passthrough fix
# in db58919 covers the conversion to SVT-AV1-HDR's non-spec
# storage; this fixture only verifies the output AV1
# bitstream replays the same values.
# ============================================================
HDR_CLIP="$CACHE_DIR/hdr_clip.mkv"
HDR_MASTER_DISPLAY="G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(40000000,50)"
HDR_MAX_CLL="1000,400"

if [[ $FORCE -eq 1 || ! -f "$HDR_CLIP" ]]; then
    log "synthesizing HDR10 fixture -> $HDR_CLIP (2s 3840x2160 HEVC HDR10)"
    # libx265 --master-display + --max-cll inject the SEI metadata
    # that the HDR probe in video_encode.c reads from frame side
    # data. We keep it short (2 sec) — encoding 4K HEVC eats CPU
    # but the cache means we only pay this cost once per CI runner.
    # `preset=ultrafast` belongs on ffmpeg's -preset flag, not in
    # x265-params (the wrapper warns + ignores it there).
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "testsrc2=size=3840x2160:rate=24:duration=2" \
        -pix_fmt yuv420p10le \
        -color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc -color_range tv \
        -c:v libx265 -tag:v hvc1 -preset ultrafast \
        -x265-params "master-display=${HDR_MASTER_DISPLAY}:max-cll=${HDR_MAX_CLL}:hdr10=1:repeat-headers=1:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:range=limited" \
        "$HDR_CLIP"
    log "HDR clip ready: $(du -h "$HDR_CLIP" | awk '{print $1}')"
else
    log "HDR clip already present: $HDR_CLIP"
fi

log "real-content fixtures ready under $CACHE_DIR"
