#!/usr/bin/env bash
# build_tarball.sh — produce a reproducible .tar.zst release artifact
# from a finished vmavificient build tree.
#
# Output: vmavificient-<VERSION>-<TRIPLE>.tar.zst in --out-dir,
# containing the cmake --install layout:
#
#   bin/vmavificient
#   share/vmavificient/tessdata/eng.traineddata   (when bundled)
#   share/doc/vmavificient/{LICENSE,LICENSE-BINARY,README.md}
#
# Usage:
#   packaging/tarball/build_tarball.sh \
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
            echo "build_tarball.sh: unknown arg $1" >&2; exit 2;;
    esac
done

if [[ -z "$BUILD_DIR" || -z "$TRIPLE" || -z "$OUT_DIR" ]]; then
    echo "build_tarball.sh: --build-dir, --triple, --out-dir all required" >&2
    exit 2
fi

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -z "$VERSION" ]]; then
    VERSION="$(cat "$repo_root/VERSION")"
fi
if [[ -z "$SDE" ]]; then
    # Match the reproducibility script's default: VERSION file mtime.
    SDE="$(date -r "$repo_root/VERSION" +%s)"
fi

require() {
    if ! command -v "$1" > /dev/null 2>&1; then
        echo "build_tarball.sh: missing required tool: $1" >&2
        exit 2
    fi
}
require cmake
require tar
require zstd

log() { printf '[build_tarball] %s\n' "$*"; }

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

log "installing $BUILD_DIR -> $stage (SOURCE_DATE_EPOCH=$SDE)"
SOURCE_DATE_EPOCH="$SDE" cmake --install "$BUILD_DIR" --prefix "$stage" --strip

mkdir -p "$OUT_DIR"
out_file="$OUT_DIR/vmavificient-${VERSION}-${TRIPLE}.tar.zst"
log "building $out_file"

# Reproducible tar discipline:
#   --owner=0 --group=0          strip uid/gid
#   --sort=name                  fixed entry ordering
#   --mtime=@SDE                 fixed timestamps
#   --format=ustar               pinned archive format (no ext attrs that vary by host)
#
# We require GNU tar (`gtar` on macOS via brew, system `tar` on Linux):
# bsdtar's reproducibility surface is fiddly enough that the per-arch
# build outputs drift even with identical input. GNU tar's --sort +
# --mtime combo gives byte-identical archives across runs.
tar_bin="tar"
if ! tar --version 2>/dev/null | grep -q "GNU tar"; then
    if command -v gtar > /dev/null 2>&1; then
        tar_bin="gtar"
    else
        echo "build_tarball.sh: GNU tar required (install via 'brew install gnu-tar' on macOS)" >&2
        exit 2
    fi
fi
"$tar_bin" --format=ustar --owner=0 --group=0 \
    --sort=name --mtime="@${SDE}" \
    -C "$stage" -cf - . \
    | zstd --no-progress --threads=0 -19 -o "$out_file"

# Sanity: re-list contents so the build log shows what landed in the
# archive. zstd -dc | tar -tvf surfaces the layout without extracting.
log "archive contents:"
zstd -dc "$out_file" | tar -tvf - | sed 's/^/  /'
log "wrote $(du -h "$out_file" | awk '{print $1}'): $out_file"
