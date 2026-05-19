#!/usr/bin/env bash
# Download and install a pinned Zig toolchain. Zig is used as a drop-in
# Clang frontend with bundled musl/glibc/Win32 headers, replacing
# musl-cross-make + mingw-w64 + cross-binutils wrangling.
#
# Usage:
#   scripts/fetch_zig.sh [install_dir]
# After running, $install_dir/bin/zig is on disk; the caller should add it
# to PATH (in GitHub Actions, `echo "$install_dir" >> "$GITHUB_PATH"`).
set -euo pipefail

ZIG_VERSION="${ZIG_VERSION:-0.14.0}"
INSTALL_DIR="${1:-${HOME}/.toolchains/zig-${ZIG_VERSION}}"

case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)   ARCH="linux-x86_64" ;;
    Linux-aarch64)  ARCH="linux-aarch64" ;;
    Darwin-x86_64)  ARCH="macos-x86_64" ;;
    Darwin-arm64)   ARCH="macos-aarch64" ;;
    *) echo "fetch_zig: unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

URL="https://ziglang.org/download/${ZIG_VERSION}/zig-${ARCH}-${ZIG_VERSION}.tar.xz"
TARBALL="$(mktemp -t zig.XXXXXX.tar.xz)"
trap 'rm -f "$TARBALL"' EXIT

if [ -x "${INSTALL_DIR}/zig" ]; then
    echo "fetch_zig: already installed at ${INSTALL_DIR}"
else
    echo "fetch_zig: downloading ${URL}"
    curl --fail --silent --show-error --location -o "${TARBALL}" "${URL}"
    mkdir -p "${INSTALL_DIR}"
    tar -xJf "${TARBALL}" -C "${INSTALL_DIR}" --strip-components=1
fi

"${INSTALL_DIR}/zig" version
echo "${INSTALL_DIR}"
