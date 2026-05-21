# Linux x86_64 musl toolchain — uses `zig cc` / `zig c++` as drop-in
# Clang frontends. Bootstrapped by scripts/fetch_zig.sh, which puts
# `zig`, `zig-cc`, `zig-c++`, `zig-ar`, and `zig-ranlib` on PATH.
# The cc/c++/ar/ranlib wrappers exist because CMake's `-P` script mode
# invokes ${CMAKE_*_COMPILER} directly, ignoring *_COMPILER_ARG1.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(_zig_cc     zig-cc     REQUIRED)
find_program(_zig_cxx    zig-c++    REQUIRED)
find_program(_zig_ar     zig-ar     REQUIRED)
find_program(_zig_ranlib zig-ranlib REQUIRED)

set(CMAKE_C_COMPILER   "${_zig_cc}")
set(CMAKE_CXX_COMPILER "${_zig_cxx}")
set(CMAKE_AR           "${_zig_ar}"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       "${_zig_ranlib}" CACHE FILEPATH "" FORCE)

set(_target "x86_64-linux-musl")
set(CMAKE_C_FLAGS_INIT             "-target ${_target}")
set(CMAKE_CXX_FLAGS_INIT           "-target ${_target}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-target ${_target} -static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target ${_target}")

# CMake 3.27+ emits --dependency-file=<path> to the linker for ninja
# dep tracking. zig cc's lld wrapper has a whitelist that rejects it
# as "unsupported linker arg". Force the probe FALSE so CMake skips
# adding the flag entirely.
set(CMAKE_C_LINKER_DEPFILE_SUPPORTED   FALSE)
set(CMAKE_CXX_LINKER_DEPFILE_SUPPORTED FALSE)
