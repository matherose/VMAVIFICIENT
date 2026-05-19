#!/usr/bin/env bash
# Download and install a pinned llvm-mingw toolchain — LLVM + lld targeting
# the MinGW-w64 CRT. Used to cross-compile vmavificient for Windows from a
# Linux runner without MSVC.
#
# Usage:
#   scripts/fetch_llvm_mingw.sh [install_dir]
# After running, $install_dir/bin/x86_64-w64-mingw32-clang is on disk; the
# caller should add $install_dir/bin to PATH.
set -euo pipefail

LLVM_MINGW_RELEASE="${LLVM_MINGW_RELEASE:-20241217}"
LLVM_MINGW_VERSION="${LLVM_MINGW_VERSION:-llvm-mingw-${LLVM_MINGW_RELEASE}-ucrt-ubuntu-20.04-x86_64}"
INSTALL_DIR="${1:-${HOME}/.toolchains/${LLVM_MINGW_VERSION}}"

case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) ;;
    Darwin-*)     LLVM_MINGW_VERSION="llvm-mingw-${LLVM_MINGW_RELEASE}-ucrt-macos-universal" ;;
    *) echo "fetch_llvm_mingw: unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_RELEASE}/${LLVM_MINGW_VERSION}.tar.xz"
TARBALL="$(mktemp -t llvm-mingw.XXXXXX.tar.xz)"
trap 'rm -f "$TARBALL"' EXIT

if [ -x "${INSTALL_DIR}/bin/x86_64-w64-mingw32-clang" ]; then
    echo "fetch_llvm_mingw: already installed at ${INSTALL_DIR}"
else
    echo "fetch_llvm_mingw: downloading ${URL}"
    curl --fail --silent --show-error --location -o "${TARBALL}" "${URL}"
    mkdir -p "${INSTALL_DIR}"
    tar -xJf "${TARBALL}" -C "${INSTALL_DIR}" --strip-components=1
fi

"${INSTALL_DIR}/bin/x86_64-w64-mingw32-clang" --version
echo "${INSTALL_DIR}"
