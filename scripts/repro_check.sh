#!/usr/bin/env bash
# repro_check.sh — gate that the vmavificient binary is byte-identical
# across two clean builds with the same SOURCE_DATE_EPOCH.
#
# Usage:
#   scripts/repro_check.sh [--preset <name>] [--source-date-epoch <ts>]
#
# Options:
#   --preset             CMake preset to drive both builds (default: env
#                        VMAV_REPRO_PRESET, else linux-x86_64).
#   --source-date-epoch  Override the epoch (default: VERSION file mtime).
#
# Exit codes:
#   0  — sha256 matches; reproducibility confirmed.
#   1  — sha256 differs; reproducibility broken.
#   2  — invocation or build error.
#
# The script performs two full clean builds in the SAME directory
# (build/repro), wiping it between rounds. We deliberately do NOT use
# distinct per-round directories — third-party deps (Tesseract, OpenSSL,
# FFmpeg) bake their absolute build paths into objects via __FILE__-style
# strings, so two different parent paths would never produce identical
# binaries until every ExternalProject also gets -ffile-prefix-map flags
# threaded through its configure/meson/cargo invocations. That's a
# Phase 6 concern; what we gate here is the stronger and simpler
# guarantee: given the same source tree at the same path, two clean
# builds produce byte-identical output. If diffoscope is on PATH, a
# side-by-side decomposition is printed on mismatch to speed up triage.

set -euo pipefail

preset="${VMAV_REPRO_PRESET:-linux-x86_64}"
sde="${SOURCE_DATE_EPOCH:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --preset)
            [ $# -ge 2 ] || { echo "repro_check: --preset needs a value" >&2; exit 2; }
            preset="$2"; shift 2 ;;
        --source-date-epoch)
            [ $# -ge 2 ] || { echo "repro_check: --source-date-epoch needs a value" >&2; exit 2; }
            sde="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *)
            echo "repro_check: unknown arg: $1" >&2; exit 2 ;;
    esac
done

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# Pin SOURCE_DATE_EPOCH for both rounds. Default matches the fallback in
# VmavReproducible.cmake (VERSION file mtime, UTC).
if [ -z "$sde" ]; then
    if stat -c %Y VERSION >/dev/null 2>&1; then
        sde="$(stat -c %Y VERSION)"       # GNU coreutils
    else
        sde="$(stat -f %m VERSION)"        # BSD / macOS
    fi
fi
export SOURCE_DATE_EPOCH="$sde"

# Binary path inside the build tree (preset-specific).
case "$preset" in
    windows-*) bin_relpath="src/vmavificient.exe" ;;
    *)         bin_relpath="src/vmavificient"     ;;
esac

# Both rounds use the same build dir, wiped fully between rounds.
# This is the STRONG contract — the dep tree is rebuilt from scratch
# in each round, so we validate that *the entire pipeline* (vendored
# build systems included) is deterministic, not just our project's
# link against a frozen install. CI only invokes this script in
# "strong" mode (push to main, push to tags); branch-push CI uses
# the cached fast path via `cmake --build` directly without this
# script and accepts the determinism flags as proof-by-construction.
build_dir="build/${preset}"

# IMPORTANT: every informational echo in this function must go to stderr.
# Callers capture run_round's stdout into $(...) to read the sha256 — if
# the "==> [round-a] ..." labels leaked into stdout, the equality check
# below would diff on the label strings, not on the actual binary, and
# wrongly report a mismatch when the build is in fact reproducible.
run_round() {
    local label="$1"

    echo "==> [round-${label}] cleaning ${build_dir}" >&2
    rm -rf "$build_dir"

    echo "==> [round-${label}] configure" >&2
    cmake --preset "$preset" >&2

    echo "==> [round-${label}] build" >&2
    cmake --build "$build_dir" --target vmavificient >&2

    local snapshot="/tmp/vmav-repro-${label}"
    cp "$build_dir/$bin_relpath" "$snapshot"
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$snapshot" | awk '{print $1}'
    else
        sha256sum "$snapshot" | awk '{print $1}'
    fi
}

echo "repro_check: preset=${preset} SOURCE_DATE_EPOCH=${sde}"

sha_a="$(run_round a)"
echo "repro_check: round-a sha256=${sha_a}"

sha_b="$(run_round b)"
echo "repro_check: round-b sha256=${sha_b}"

if [ "$sha_a" = "$sha_b" ]; then
    echo "repro_check: REPRODUCIBLE — ${sha_a}"
    exit 0
fi

echo "repro_check: MISMATCH" >&2
echo "  round-a: ${sha_a}" >&2
echo "  round-b: ${sha_b}" >&2

if command -v diffoscope >/dev/null 2>&1; then
    echo "repro_check: running diffoscope for triage" >&2
    diffoscope /tmp/vmav-repro-a /tmp/vmav-repro-b >&2 || true
else
    echo "repro_check: install diffoscope (apt install diffoscope) for diff details" >&2
fi
exit 1
