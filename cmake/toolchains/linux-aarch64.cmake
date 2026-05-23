# Linux aarch64 native toolchain — apt-installed clang-20 + lld-20.
# Build runs on a native aarch64 Linux host (ubuntu-24.04-arm on CI).
# No cross-compile, no sysroot plumbing. Binary dynamic-links system
# glibc; vendored deps stay statically linked into the binary.
#
# See project memory entry "Toolchain: native + glibc" for the full
# rationale on why we dropped musl-static.
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

set(CMAKE_EXE_LINKER_FLAGS_INIT    "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
