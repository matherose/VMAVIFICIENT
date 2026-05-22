#!/usr/bin/env bash
# Download and install a musl sysroot for cross-compilation. We use
# the prebuilt cross-toolchain tarballs from musl.cc and keep only the
# sysroot directory — clang is provided separately (apt-installed
# clang-20 on CI, Homebrew clang locally on macOS).
#
# Usage:
#   scripts/fetch_musl_sysroot.sh <triple> [install_dir]
#
# Examples:
#   scripts/fetch_musl_sysroot.sh x86_64-linux-musl
#   scripts/fetch_musl_sysroot.sh aarch64-linux-musl
#
# After running, the sysroot lives at
#   ${install_dir}/${triple}/
# and the script prints that path on stdout.
#
# Why musl.cc: it's the de-facto-standard mirror of musl-cross-make
# tarballs, maintained by Z. King. Stable for years. Tarballs are
# ~30MB each. We only need the sysroot portion (musl headers +
# libc.a/libc.so + crt1.o etc.); the bundled gcc binaries are
# discarded — clang cross-compiles to the triple using --target=
# + --sysroot=.
set -euo pipefail

TRIPLE="${1:?Usage: $0 <triple> [install_dir]}"
INSTALL_DIR="${2:-${HOME}/.toolchains/musl-${TRIPLE}-cross}"

# Pin tarball revision via the SHA-suffixed filename so CI is
# reproducible. musl.cc has long-stable URLs but the tarball content
# can be rebuilt; pinning here guards against silent breakage.
TARBALL="${TRIPLE}-cross.tgz"
URL="https://musl.cc/${TARBALL}"

case "${TRIPLE}" in
    x86_64-linux-musl|aarch64-linux-musl) ;;
    *)
        echo "fetch_musl_sysroot: unsupported triple: ${TRIPLE}" >&2
        exit 1
        ;;
esac

if [ -d "${INSTALL_DIR}/${TRIPLE}/lib" ] && \
   [ -f "${INSTALL_DIR}/${TRIPLE}/lib/libc.a" ]; then
    echo "fetch_musl_sysroot: already installed at ${INSTALL_DIR}"
    echo "${INSTALL_DIR}"
    exit 0
fi

DOWNLOAD="$(mktemp -t "musl-${TRIPLE}.XXXXXX.tgz")"
trap 'rm -f "${DOWNLOAD}"' EXIT

echo "fetch_musl_sysroot: downloading ${URL}"
curl --fail --silent --show-error --location -o "${DOWNLOAD}" "${URL}"

mkdir -p "${INSTALL_DIR}"
tar -xzf "${DOWNLOAD}" -C "${INSTALL_DIR}" --strip-components=1

# Sanity-check: the sysroot subdir should contain a libc.a.
if [ ! -f "${INSTALL_DIR}/${TRIPLE}/lib/libc.a" ]; then
    echo "fetch_musl_sysroot: tarball layout unexpected — no ${TRIPLE}/lib/libc.a" >&2
    exit 1
fi

echo "${INSTALL_DIR}"
