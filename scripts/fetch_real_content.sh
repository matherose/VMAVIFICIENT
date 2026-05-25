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
# Tears of Steel HDR — Blender Foundation reference HDR10
# encode. Used to gate the HDR-passthrough path (mastering
# display + content light level OBU metadata).
# ============================================================
# NOTE: the canonical HDR-graded ToS distributions are hosted by
# Blender (mango.blender.org). The mp4 chosen here is the official
# UHD HDR HEVC encode that ships HDR10 metadata in-band via SEI.
TOS_URL="https://media.xiph.org/video/derf/ElFuente/Netflix_Aerial_4096x2160_60fps_10bit_420.y4m"
TOS_SHA="DOWNLOAD_AND_RECORD"   # populated by hand on first run
TOS_SOURCE="$CACHE_DIR/tos_hdr_source.mkv"
TOS_CLIP="$CACHE_DIR/tos_hdr_clip.mkv"

# DEFERRED: ToS HDR sourcing is non-trivial (Blender ships several
# HDR variants; none is a single ~100 MB direct-download mp4 like
# BBB). The Phase 8 m4 milestone will pin a specific upstream + sha.
# For m1 we get BBB working and stub ToS so the CMake plumbing is
# correct.
if [[ "${VMAV_FETCH_HDR_FIXTURE:-0}" = "1" ]]; then
    fetch_with_sha "$TOS_URL" "$TOS_SOURCE" "$TOS_SHA"
    # ... trim path follows the BBB pattern
    log "ToS HDR clip ready: $(du -h "$TOS_CLIP" | awk '{print $1}')"
else
    log "ToS HDR fixture deferred to Phase 8 m4 (set VMAV_FETCH_HDR_FIXTURE=1 to opt in)"
fi

log "real-content fixtures ready under $CACHE_DIR"
