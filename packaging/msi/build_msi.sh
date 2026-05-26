#!/usr/bin/env bash
# build_msi.sh — produce a Windows .msi release artifact via WiX v4
# from a finished vmavificient build tree (windows-x86_64-mingw only).
#
# This runs on Linux CI (or anywhere dotnet 6+ is installed); WiX v4
# is a .NET tool that cross-builds MSIs without Windows.
#
# Output: vmavificient-<VERSION>-windows-x86_64.msi in --out-dir.
#
# Installer behavior:
#   * Installs to %ProgramFiles%\vmavificient\
#   * Adds %ProgramFiles%\vmavificient to system PATH
#   * Sets TESSDATA_PREFIX system env to the tessdata subdir (so the
#     OCR step finds eng.traineddata without binary-relative search)
#   * perMachine scope (needs admin to install/uninstall)
#
# Usage:
#   packaging/msi/build_msi.sh \
#       --build-dir build/windows-x86_64-mingw \
#       --triple    windows-x86_64-mingw \
#       --out-dir   dist/

set -euo pipefail

BUILD_DIR=""
TRIPLE=""
OUT_DIR=""
VERSION=""
SDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2;;
        --triple)    TRIPLE="$2";    shift 2;;
        --out-dir)   OUT_DIR="$2";   shift 2;;
        --version)   VERSION="$2";   shift 2;;
        --source-date-epoch) SDE="$2"; shift 2;;
        -h|--help)
            sed -n '2,20p' "$0"; exit 0;;
        *)
            echo "build_msi.sh: unknown arg $1" >&2; exit 2;;
    esac
done

if [[ -z "$BUILD_DIR" || -z "$TRIPLE" || -z "$OUT_DIR" ]]; then
    echo "build_msi.sh: --build-dir, --triple, --out-dir all required" >&2
    exit 2
fi

case "$TRIPLE" in
    windows-x86_64-mingw) ;;  # supported
    *)
        echo "build_msi.sh: triple '$TRIPLE' is not a Windows target" >&2
        exit 2;;
esac

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -z "$VERSION" ]]; then
    VERSION="$(cat "$repo_root/VERSION")"
fi
if [[ -z "$SDE" ]]; then
    SDE="$(stat -f %m "$repo_root/VERSION" 2>/dev/null || stat -c %Y "$repo_root/VERSION")"
fi

# WiX MSI versions must be N.N.N or N.N.N.N (numeric only). VERSION
# files like "2.0.0-dev" need stripping to satisfy MSI's parser; the
# user-visible version string in Add/Remove Programs is the cleaned
# value, but the file's product identity is still pinned via UpgradeCode.
MSI_VERSION="$(echo "$VERSION" | sed -E 's/[^0-9.].*$//')"
if [[ -z "$MSI_VERSION" ]]; then
    echo "build_msi.sh: VERSION '$VERSION' has no numeric prefix" >&2
    exit 2
fi

require() {
    if ! command -v "$1" > /dev/null 2>&1; then
        echo "build_msi.sh: missing required tool: $1" >&2
        exit 2
    fi
}
require cmake
require wix

log() { printf '[build_msi] %s\n' "$*"; }

stage="$(mktemp -d)"
trap "rm -rf '$stage'" EXIT

log "installing $BUILD_DIR -> $stage (no prefix; tree mirrors target layout)"
# We install with --prefix=stage so the staging layout is bin/...,
# share/..., etc. The .wxs file references these by absolute path
# (we substitute @STAGE@ below). Strip removes debug symbols.
SOURCE_DATE_EPOCH="$SDE" cmake --install "$BUILD_DIR" \
    --prefix "$stage" --strip

# Substitute template tokens into a copy of the .wxs.
wxs_in="$repo_root/packaging/msi/vmavificient.wxs.in"
wxs_out="$stage/vmavificient.wxs"
sed -e "s|@VERSION@|${MSI_VERSION}|g" \
    -e "s|@STAGE@|${stage}|g" \
    "$wxs_in" > "$wxs_out"

mkdir -p "$OUT_DIR"
out_file="$OUT_DIR/vmavificient-${VERSION}-windows-x86_64.msi"

log "wix build -> $out_file (msi version=$MSI_VERSION)"
# -arch x64 targets 64-bit MSI tables. -d sets WiX preprocessor vars
# (none used here but the flag is reserved for future use). The .msi
# is the only output; -intermediateFolder keeps stage tidy.
SOURCE_DATE_EPOCH="$SDE" wix build \
    -arch x64 \
    -intermediateFolder "$stage/wix-int" \
    -out "$out_file" \
    "$wxs_out"

log "wrote $(du -h "$out_file" | awk '{print $1}'): $out_file"
