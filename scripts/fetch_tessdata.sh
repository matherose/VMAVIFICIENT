#!/usr/bin/env bash
# fetch_tessdata.sh — pin eng.traineddata into a local cache so the
# packaging artifacts can bundle it alongside the binary.
#
# Phase 9 (packaging) needs the OCR language data shipped with each
# .deb / .pkg / .msi / .tar.zst so PGS-subtitle conversion works
# out of the box on a clean install. tessdata is too big (~22 MB)
# to commit to git, so we fetch on demand + sha256-validate against
# a pinned release.
#
# Usage:
#   scripts/fetch_tessdata.sh [--cache-dir <path>] [--force]
#
# Default cache dir: <repo>/tests/fixtures/.tessdata-cache/
# (same gitignored area as the real-content fixtures).

set -euo pipefail

CACHE_DIR=""
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --cache-dir) CACHE_DIR="$2"; shift 2;;
        --force) FORCE=1; shift;;
        -h|--help)
            sed -n '2,18p' "$0"; exit 0;;
        *)
            echo "fetch_tessdata.sh: unknown arg $1" >&2; exit 2;;
    esac
done

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "$CACHE_DIR" ]]; then
    CACHE_DIR="$repo_root/tests/fixtures/.tessdata-cache"
fi
mkdir -p "$CACHE_DIR"

log() { printf '[fetch_tessdata] %s\n' "$*"; }

require() {
    if ! command -v "$1" > /dev/null 2>&1; then
        echo "fetch_tessdata.sh: missing required tool: $1" >&2
        exit 2
    fi
}
require curl
require shasum
sha256() { shasum -a 256 "$1" | awk '{print $1}'; }

# eng.traineddata from tesseract-ocr/tessdata @ tag 4.1.0. This is the
# last release with the legacy + LSTM models in a single file (the 5.x
# trees split into _fast / _best / _legacy). Has been the de-facto
# default for Tesseract 4 + 5 since 2019.
TESSDATA_URL="https://github.com/tesseract-ocr/tessdata/raw/4.1.0/eng.traineddata"
TESSDATA_SHA="daa0c97d651c19fba3b25e81317cd697e9908c8208090c94c3905381c23fc047"
TESSDATA_OUT="$CACHE_DIR/eng.traineddata"

if [[ $FORCE -eq 1 || ! -f "$TESSDATA_OUT" ]]; then
    log "downloading $TESSDATA_URL"
    curl --fail --location --silent --show-error \
        --output "$TESSDATA_OUT.tmp" "$TESSDATA_URL"
    mv "$TESSDATA_OUT.tmp" "$TESSDATA_OUT"
fi

got="$(sha256 "$TESSDATA_OUT")"
if [[ "$got" != "$TESSDATA_SHA" ]]; then
    echo "fetch_tessdata.sh: sha256 mismatch for $TESSDATA_OUT" >&2
    echo "  expected $TESSDATA_SHA" >&2
    echo "  got      $got" >&2
    rm -f "$TESSDATA_OUT"
    exit 1
fi

log "tessdata ready: $(du -h "$TESSDATA_OUT" | awk '{print $1}') at $TESSDATA_OUT"
