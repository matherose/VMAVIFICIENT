#!/usr/bin/env bash
# Download and install a musl sysroot for cross-compilation. We use
# Bootlin's prebuilt toolchain tarballs and keep only the sysroot —
# clang is provided separately (apt-installed clang-20 on CI).
#
# Why Bootlin: musl.cc is intermittently unreachable from GHA runners
# (cloud-IP throttling); Bootlin's Apache-served toolchains.bootlin.com
# is reliably available from CI. Tarballs ship a stable layout:
#     <tarball-name>/<triple>/sysroot/        ← the sysroot we want
# where <triple> is the buildroot-style triple, e.g.
# `aarch64-buildroot-linux-musl`. clang doesn't care about the triple
# encoded in the sysroot path — only what we pass to `--target=`.
#
# Usage:
#   scripts/fetch_musl_sysroot.sh <triple> [install_dir]
#
# Examples:
#   scripts/fetch_musl_sysroot.sh x86_64-linux-musl
#   scripts/fetch_musl_sysroot.sh aarch64-linux-musl
#
# Prints the sysroot path on stdout (the `<triple>/sysroot/` subdir).
set -euo pipefail

TRIPLE="${1:?Usage: $0 <triple> [install_dir]}"
INSTALL_BASE="${2:-${HOME}/.toolchains/musl-sysroots}"

# Bootlin's stable 2024.02-1 release. Pin specifically — Bootlin
# publishes new releases occasionally and we want reproducible CI.
BOOTLIN_RELEASE="2024.02-1"

case "${TRIPLE}" in
    x86_64-linux-musl)
        # x86-64-v3 requires Haswell+ (AVX2). We can ship binaries
        # compiled against this sysroot to any x86_64 host because
        # the sysroot's libc.a contains generic code; runtime SIMD
        # is what bumps the floor, and our actual code paths gate
        # SIMD per-feature.
        _bootlin_arch="x86-64-v3"
        _bootlin_triple="x86_64-buildroot-linux-musl"
        ;;
    aarch64-linux-musl)
        _bootlin_arch="aarch64"
        _bootlin_triple="aarch64-buildroot-linux-musl"
        ;;
    *)
        echo "fetch_musl_sysroot: unsupported triple: ${TRIPLE}" >&2
        exit 1
        ;;
esac

TARBALL_NAME="${_bootlin_arch}--musl--stable-${BOOTLIN_RELEASE}"
URL="https://toolchains.bootlin.com/downloads/releases/toolchains/${_bootlin_arch}/tarballs/${TARBALL_NAME}.tar.bz2"

INSTALL_DIR="${INSTALL_BASE}/${TRIPLE}"
SYSROOT="${INSTALL_DIR}/${_bootlin_triple}/sysroot"

if [ -f "${SYSROOT}/lib/libc.a" ] || [ -f "${SYSROOT}/usr/lib/libc.a" ]; then
    echo "fetch_musl_sysroot: already installed at ${SYSROOT}" >&2
    echo "${SYSROOT}"
    exit 0
fi

DOWNLOAD="$(mktemp -t "musl-${TRIPLE}.XXXXXX.tar.bz2")"
trap 'rm -f "${DOWNLOAD}"' EXIT

echo "fetch_musl_sysroot: downloading ${URL}" >&2
curl --fail --silent --show-error --location --max-time 600 \
    -o "${DOWNLOAD}" "${URL}"

mkdir -p "${INSTALL_DIR}"
tar -xjf "${DOWNLOAD}" -C "${INSTALL_DIR}" --strip-components=1

# Sanity-check the layout. Bootlin sysroots have libc.a in usr/lib
# (or lib for some builds). Either works for clang's --sysroot.
if [ ! -f "${SYSROOT}/lib/libc.a" ] && [ ! -f "${SYSROOT}/usr/lib/libc.a" ]; then
    echo "fetch_musl_sysroot: tarball layout unexpected — no libc.a in ${SYSROOT}" >&2
    ls -la "${SYSROOT}" >&2 || true
    exit 1
fi

echo "${SYSROOT}"
