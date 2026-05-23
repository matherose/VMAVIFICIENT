#!/usr/bin/env bash
# Diagnostic wrapper around FFmpeg's configure script. Prints the
# state of the opus install dir + pkg-config probe BEFORE invoking
# the actual configure, so CI logs make it clear what went wrong if
# the libopus detection fails. Pass-through for the actual configure
# invocation so behavior is unchanged on success.
#
# Usage (from VmavThirdParty.cmake):
#   bash scripts/ffmpeg-configure-wrapper.sh <opus_install> <ffmpeg_configure> [configure-args...]
set -eu

OPUS_INSTALL="${1:?opus install dir}"
FFMPEG_CONFIGURE="${2:?ffmpeg configure path}"
shift 2

echo "=== diagnostic: opus install layout ==="
ls -la "${OPUS_INSTALL}" || true
echo "--- opus/lib ---"
ls -la "${OPUS_INSTALL}/lib" || true
echo "--- opus/lib/pkgconfig ---"
ls -la "${OPUS_INSTALL}/lib/pkgconfig" || true
echo "--- opus.pc content ---"
cat "${OPUS_INSTALL}/lib/pkgconfig/opus.pc" 2>&1 || true

echo "=== diagnostic: pkg-config probe ==="
echo "which pkg-config: $(command -v pkg-config || echo '<missing>')"
echo "pkg-config --version: $(pkg-config --version 2>&1 || true)"
echo "PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR:-<unset>}"
echo "PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-<unset>}"
pkg-config --exists opus && echo "pkg-config: opus FOUND" \
    || echo "pkg-config: opus NOT FOUND (exit=$?)"
pkg-config --list-all 2>&1 | head -20

echo "=== invoking FFmpeg configure ==="
exec "${FFMPEG_CONFIGURE}" "$@"
