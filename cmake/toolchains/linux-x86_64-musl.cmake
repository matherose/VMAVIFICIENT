# Linux x86_64 musl toolchain — apt-installed clang + lld + prebuilt
# musl sysroot from musl.cc. See project memory entry
# "Toolchain: LLVM only" for why we switched from zig cc in m1.
#
# Required environment / setup:
#   * `clang` and `lld` on PATH (apt-installed clang-20+ recommended
#     so SVT-AV1's NEON .S files and AVX-512 intrinsics compile cleanly).
#   * VMAV_MUSL_SYSROOT pointing at the `x86_64-linux-musl/` subdir of
#     a musl-cross-make-style tarball — produced by
#     `scripts/fetch_musl_sysroot.sh x86_64-linux-musl`.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(_clang   NAMES clang-20 clang   REQUIRED)
find_program(_clangxx NAMES clang++-20 clang++ REQUIRED)
find_program(_ar      NAMES llvm-ar-20 llvm-ar ar)
find_program(_ranlib  NAMES llvm-ranlib-20 llvm-ranlib ranlib)

set(CMAKE_C_COMPILER   "${_clang}")
set(CMAKE_CXX_COMPILER "${_clangxx}")
if(_ar)
    set(CMAKE_AR     "${_ar}"     CACHE FILEPATH "" FORCE)
endif()
if(_ranlib)
    set(CMAKE_RANLIB "${_ranlib}" CACHE FILEPATH "" FORCE)
endif()

# musl sysroot path. Caller must set VMAV_MUSL_SYSROOT (in CI: env;
# locally: `cmake -DVMAV_MUSL_SYSROOT=$(scripts/fetch_musl_sysroot.sh
# x86_64-linux-musl)/x86_64-linux-musl --preset linux-x86_64-musl`).
if(NOT DEFINED VMAV_MUSL_SYSROOT)
    if(DEFINED ENV{VMAV_MUSL_SYSROOT})
        set(VMAV_MUSL_SYSROOT "$ENV{VMAV_MUSL_SYSROOT}")
    else()
        message(FATAL_ERROR
            "linux-x86_64-musl toolchain: VMAV_MUSL_SYSROOT not set.\n"
            "Run scripts/fetch_musl_sysroot.sh x86_64-linux-musl and set\n"
            "  VMAV_MUSL_SYSROOT  = <bootlin-base>/<triple>/sysroot\n"
            "  VMAV_GCC_TOOLCHAIN = <bootlin-base>")
    endif()
endif()
if(NOT DEFINED VMAV_GCC_TOOLCHAIN)
    if(DEFINED ENV{VMAV_GCC_TOOLCHAIN})
        set(VMAV_GCC_TOOLCHAIN "$ENV{VMAV_GCC_TOOLCHAIN}")
    else()
        message(FATAL_ERROR
            "linux-x86_64-musl toolchain: VMAV_GCC_TOOLCHAIN not set "
            "(needed for crtbeginT.o / libgcc.a lookup).")
    endif()
endif()

# Use Bootlin's buildroot-style triple for the clang --target. clang
# locates the GCC support files at
#   <gcc-toolchain>/lib/gcc/<target>/<version>/
# and Bootlin stores them under `x86_64-buildroot-linux-musl`, not
# `x86_64-linux-musl`. The vendor string in the triple is cosmetic for
# the code generator but load-bearing for the file lookup, so we
# match exactly. CMAKE_SYSTEM_PROCESSOR stays "x86_64" — that's what
# downstream code (third-party deps' CPU-feature probes) looks at.
set(_target "x86_64-buildroot-linux-musl")
# `--gcc-toolchain=` makes clang find the GCC support files (crt*.o,
# libgcc.a, libgcc_eh.a) bundled in Bootlin's tarball. Without it clang
# emits `cannot open crtbeginT.o` + `unable to find library -lgcc`.
set(_sysroot_flags
    "--target=${_target} --sysroot=${VMAV_MUSL_SYSROOT} --gcc-toolchain=${VMAV_GCC_TOOLCHAIN}")
set(CMAKE_C_FLAGS_INIT             "${_sysroot_flags}")
set(CMAKE_CXX_FLAGS_INIT           "${_sysroot_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_sysroot_flags} -fuse-ld=lld -static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_sysroot_flags} -fuse-ld=lld")
set(CMAKE_ASM_FLAGS_INIT           "${_sysroot_flags}")

# Skip CMake's host-system search for libs/includes during cross
# probes; the sysroot is authoritative.
set(CMAKE_SYSROOT "${VMAV_MUSL_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
