# Linux aarch64 musl toolchain — apt-installed clang + lld + prebuilt
# musl sysroot from musl.cc. See project memory entry
# "Toolchain: LLVM only" for why we switched from zig cc in m1.
#
# Required environment / setup:
#   * `clang` and `lld` on PATH (apt-installed clang-20+).
#   * VMAV_MUSL_SYSROOT pointing at the `aarch64-linux-musl/` subdir of
#     a musl-cross-make tarball — produced by
#     `scripts/fetch_musl_sysroot.sh aarch64-linux-musl`.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

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

if(NOT DEFINED VMAV_MUSL_SYSROOT)
    if(DEFINED ENV{VMAV_MUSL_SYSROOT})
        set(VMAV_MUSL_SYSROOT "$ENV{VMAV_MUSL_SYSROOT}")
    else()
        message(FATAL_ERROR
            "linux-aarch64-musl toolchain: VMAV_MUSL_SYSROOT not set.\n"
            "Run scripts/fetch_musl_sysroot.sh aarch64-linux-musl and pass\n"
            "  -DVMAV_MUSL_SYSROOT=<path>/aarch64-linux-musl\n"
            "or export VMAV_MUSL_SYSROOT in the environment.")
    endif()
endif()

set(_target "aarch64-linux-musl")
set(_sysroot_flags "--target=${_target} --sysroot=${VMAV_MUSL_SYSROOT}")
set(CMAKE_C_FLAGS_INIT             "${_sysroot_flags}")
set(CMAKE_CXX_FLAGS_INIT           "${_sysroot_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_sysroot_flags} -fuse-ld=lld -static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_sysroot_flags} -fuse-ld=lld")
set(CMAKE_ASM_FLAGS_INIT           "${_sysroot_flags}")

set(CMAKE_SYSROOT "${VMAV_MUSL_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
