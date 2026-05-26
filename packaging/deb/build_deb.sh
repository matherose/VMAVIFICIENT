#!/usr/bin/env bash
# build_deb.sh — produce a reproducible .deb release artifact from a
# finished vmavificient build tree (Linux arches only).
#
# Output: vmavificient_<VERSION>_<DEB_ARCH>.deb in --out-dir.
# DEB_ARCH derives from --triple:
#   linux-x86_64  → amd64
#   linux-aarch64 → arm64
#
# Usage:
#   packaging/deb/build_deb.sh \
#       --build-dir build/linux-x86_64 \
#       --triple    linux-x86_64 \
#       --out-dir   dist/
#
# Optional:
#   --version <v>            Override the VERSION file (defaults to repo VERSION).
#   --source-date-epoch <ts> Override SOURCE_DATE_EPOCH (defaults to VERSION file mtime).

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
            echo "build_deb.sh: unknown arg $1" >&2; exit 2;;
    esac
done

if [[ -z "$BUILD_DIR" || -z "$TRIPLE" || -z "$OUT_DIR" ]]; then
    echo "build_deb.sh: --build-dir, --triple, --out-dir all required" >&2
    exit 2
fi

case "$TRIPLE" in
    linux-x86_64)  DEB_ARCH="amd64";;
    linux-aarch64) DEB_ARCH="arm64";;
    *)
        echo "build_deb.sh: triple '$TRIPLE' is not a Linux target" >&2
        exit 2;;
esac

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -z "$VERSION" ]]; then
    VERSION="$(cat "$repo_root/VERSION")"
fi
if [[ -z "$SDE" ]]; then
    SDE="$(stat -f %m "$repo_root/VERSION" 2>/dev/null || stat -c %Y "$repo_root/VERSION")"
fi

require() {
    if ! command -v "$1" > /dev/null 2>&1; then
        echo "build_deb.sh: missing required tool: $1" >&2
        exit 2
    fi
}
require cmake
require dpkg-deb

log() { printf '[build_deb] %s\n' "$*"; }

stage="$(mktemp -d)"
trap "rm -rf '$stage'" EXIT
mkdir -p "$stage/DEBIAN"

log "installing $BUILD_DIR -> $stage (prefix=/usr, SOURCE_DATE_EPOCH=$SDE)"
# Install into the staging dir with /usr prefix so cmake lays files at
# usr/bin/, usr/share/, etc — exactly the .deb layout. --strip removes
# debug symbols.
SOURCE_DATE_EPOCH="$SDE" cmake --install "$BUILD_DIR" \
    --prefix "$stage/usr" --strip

# Templated control file. The .in source has @VERSION@ + @ARCH@ tokens
# we substitute here; everything else (maintainer, depends, description)
# is static metadata.
sed -e "s/@VERSION@/${VERSION}/g" \
    -e "s/@ARCH@/${DEB_ARCH}/g" \
    "$repo_root/packaging/deb/control.in" > "$stage/DEBIAN/control"

# Copyright file. Debian policy wants this at usr/share/doc/<pkg>/copyright.
mkdir -p "$stage/usr/share/doc/vmavificient"
install -m 0644 "$repo_root/packaging/deb/copyright" \
    "$stage/usr/share/doc/vmavificient/copyright"

# Reproducibility: clamp every file's mtime to SDE before dpkg-deb
# stamps the archive. The clamp covers entries created by cmake
# --install AND the control/copyright files we just dropped in.
# GNU touch accepts -d "@epoch"; BSD touch (macOS) needs -t
# [[CC]YY]MMDDhhmm[.SS], so we branch on the host's coreutils flavor.
if date --version 2>/dev/null | grep -q "GNU coreutils"; then
    find "$stage" -depth -exec touch -h -d "@${SDE}" {} +
else
    touch_stamp="$(date -u -r "${SDE}" +'%Y%m%d%H%M.%S')"
    find "$stage" -depth -exec touch -h -t "$touch_stamp" {} +
fi

log "building .deb"
out_file="$OUT_DIR/vmavificient_${VERSION}_${DEB_ARCH}.deb"
mkdir -p "$OUT_DIR"
# --root-owner-group forces owner=root:root on every entry — required
# for reproducibility (otherwise dpkg-deb stamps the runtime uid/gid).
# SOURCE_DATE_EPOCH is honored by dpkg-deb 1.20+ for the ar member
# timestamps.
SOURCE_DATE_EPOCH="$SDE" dpkg-deb --root-owner-group --build "$stage" "$out_file"

log "wrote $(du -h "$out_file" | awk '{print $1}'): $out_file"
log "package contents:"
dpkg-deb -c "$out_file" | sed 's/^/  /'
log "package metadata:"
dpkg-deb -I "$out_file" | sed 's/^/  /'
