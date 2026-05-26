#!/usr/bin/env bash
# build_pkg.sh — produce a macOS .pkg release artifact from a finished
# vmavificient build tree (macos-arm64 only — Apple Silicon).
#
# Output: vmavificient-<VERSION>-<TRIPLE>.pkg in --out-dir.
#
# Installer layout (target machine):
#   /usr/local/bin/vmavificient
#   /usr/local/share/vmavificient/tessdata/eng.traineddata
#   /usr/local/share/doc/vmavificient/{LICENSE,LICENSE-BINARY,README.md}
#
# The package is unsigned per the original v2.0 plan; users who hit
# the Gatekeeper quarantine flag need to run:
#   xattr -d com.apple.quarantine ~/Downloads/vmavificient-*.pkg
# before double-clicking. The companion doc docs/gatekeeper-bypass.md
# walks through it.
#
# Usage:
#   packaging/pkg/build_pkg.sh \
#       --build-dir build/macos-arm64 \
#       --triple    macos-arm64 \
#       --out-dir   dist/
#
# Optional:
#   --version <v>            Override the VERSION file.
#   --source-date-epoch <ts> Override SOURCE_DATE_EPOCH.
#   --identifier <id>        Override the bundle identifier
#                            (default: com.github.matherose.vmavificient).

set -euo pipefail

BUILD_DIR=""
TRIPLE=""
OUT_DIR=""
VERSION=""
SDE=""
IDENTIFIER="com.github.matherose.vmavificient"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2;;
        --triple)    TRIPLE="$2";    shift 2;;
        --out-dir)   OUT_DIR="$2";   shift 2;;
        --version)   VERSION="$2";   shift 2;;
        --source-date-epoch) SDE="$2"; shift 2;;
        --identifier) IDENTIFIER="$2"; shift 2;;
        -h|--help)
            sed -n '2,22p' "$0"; exit 0;;
        *)
            echo "build_pkg.sh: unknown arg $1" >&2; exit 2;;
    esac
done

if [[ -z "$BUILD_DIR" || -z "$TRIPLE" || -z "$OUT_DIR" ]]; then
    echo "build_pkg.sh: --build-dir, --triple, --out-dir all required" >&2
    exit 2
fi

case "$TRIPLE" in
    macos-arm64) ;;  # supported
    *)
        echo "build_pkg.sh: triple '$TRIPLE' is not a macOS target" >&2
        exit 2;;
esac

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -z "$VERSION" ]]; then
    VERSION="$(cat "$repo_root/VERSION")"
fi
if [[ -z "$SDE" ]]; then
    SDE="$(date -r "$repo_root/VERSION" +%s)"
fi

require() {
    if ! command -v "$1" > /dev/null 2>&1; then
        echo "build_pkg.sh: missing required tool: $1" >&2
        exit 2
    fi
}
require cmake
require pkgbuild

log() { printf '[build_pkg] %s\n' "$*"; }

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

log "installing $BUILD_DIR -> $stage (prefix=/usr/local, SOURCE_DATE_EPOCH=$SDE)"
# pkgbuild lays the staging dir under --install-location, so we want
# the staging dir to mirror what should land below /usr/local.
SOURCE_DATE_EPOCH="$SDE" cmake --install "$BUILD_DIR" \
    --prefix "$stage" --strip

# Strip extended attributes from every staged file. Source files in
# the repo carry xattrs (com.apple.quarantine, com.apple.metadata:*)
# from downloads + clones across filesystems; pkgbuild encodes those
# as AppleDouble `._FILE` companions in the payload, polluting the
# package contents. None of the xattrs are user-meaningful for a CLI
# tool. `xattr -cr` clears recursively and silently no-ops on files
# without xattrs.
xattr -cr "$stage"
# Also remove any pre-existing AppleDouble files (paranoia for the
# case where the staging happened to be on a non-APFS volume).
find "$stage" -name '._*' -delete

# Clamp mtimes for deterministic Bom checksums. macOS touch (BSD)
# takes -t [[CC]YY]MMDDhhmm[.SS]; we're definitely on macOS for this
# script so no GNU branch needed.
touch_stamp="$(date -u -r "${SDE}" +'%Y%m%d%H%M.%S')"
find "$stage" -depth -exec touch -h -t "$touch_stamp" {} +

mkdir -p "$OUT_DIR"
out_file="$OUT_DIR/vmavificient-${VERSION}-${TRIPLE}.pkg"

log "building $out_file (identifier=$IDENTIFIER)"
# Caveat: macOS pkgbuild has no SOURCE_DATE_EPOCH equivalent. The xar
# archive embeds Apple-internal creation timestamps + a random-ish
# Bom uuid that drift across runs even with identical staging input.
# We clamp every input mtime above; that pins the payload's tar.xz
# inner contents but the outer xar container still varies. Accepting
# this — non-reproducibility on the .pkg outer is a tooling limitation,
# not a correctness issue. The contained binary IS reproducible (gated
# by repro_check.sh) and is the trust anchor.
#
# --ownership preserve keeps the uid/gid we just set via touch above.
# --install-location /usr/local means staging contents land at
# /usr/local/*.
pkgbuild \
    --root "$stage" \
    --identifier "$IDENTIFIER" \
    --version "$VERSION" \
    --install-location "/usr/local" \
    --ownership preserve-other \
    "$out_file"

log "wrote $(du -h "$out_file" | awk '{print $1}'): $out_file"
log "package contents:"
pkgutil --payload-files "$out_file" | sed 's/^/  /'
