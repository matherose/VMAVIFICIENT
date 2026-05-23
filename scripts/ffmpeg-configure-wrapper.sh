#!/usr/bin/env bash
# Diagnostic wrapper around FFmpeg's configure script. Prints the
# state of the opus install + pkg-config probe BEFORE invoking the
# actual configure, and dumps ffbuild/config.log on failure so CI
# logs show the actual probe error.
#
# Usage (from VmavThirdParty.cmake):
#   bash scripts/ffmpeg-configure-wrapper.sh <opus_install> <ffmpeg_configure> [configure-args...]
set -eu

OPUS_INSTALL="${1:?opus install dir}"
FFMPEG_CONFIGURE="${2:?ffmpeg configure path}"
shift 2

echo "=== diagnostic: opus install layout ==="
ls -la "${OPUS_INSTALL}/lib/pkgconfig" || true
echo "--- opus.pc content ---"
cat "${OPUS_INSTALL}/lib/pkgconfig/opus.pc" 2>&1 || true

echo "=== diagnostic: pkg-config probe ==="
echo "which pkg-config: $(command -v pkg-config || echo '<missing>')"
echo "PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR:-<unset>}"
pkg-config --exists opus && echo "pkg-config: opus FOUND" \
    || echo "pkg-config: opus NOT FOUND (exit=$?)"
echo "pkg-config opus --libs:        $(pkg-config --libs opus 2>&1 || true)"
echo "pkg-config opus --libs --static: $(pkg-config --libs --static opus 2>&1 || true)"

echo "=== invoking FFmpeg configure ==="
# Don't exec — we want to inspect config.log on failure.
if "${FFMPEG_CONFIGURE}" "$@"; then
    exit 0
fi
status=$?
echo "=== FFmpeg configure failed (exit=$status) — dumping ffbuild/config.log ==="
# `pwd` is the FFmpeg build dir set by ExternalProject_Add.
if [ -f ffbuild/config.log ]; then
    tail -200 ffbuild/config.log
else
    echo "(no ffbuild/config.log found at $(pwd))"
fi
exit "${status}"
