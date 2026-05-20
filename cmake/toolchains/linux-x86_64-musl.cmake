# Linux x86_64 musl toolchain — uses `zig cc` as a drop-in Clang.
# Bootstrapped by scripts/fetch_zig.sh, which puts `zig`, `zig-ar`, and
# `zig-ranlib` on PATH (the latter two are wrapper scripts because CMake
# can't invoke zig's "zig ar"/"zig ranlib" subcommand form directly).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(_zig        zig        REQUIRED)
find_program(_zig_ar     zig-ar     REQUIRED)
find_program(_zig_ranlib zig-ranlib REQUIRED)

set(CMAKE_C_COMPILER      "${_zig}")
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_AR              "${_zig_ar}"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB          "${_zig_ranlib}" CACHE FILEPATH "" FORCE)

set(_target "x86_64-linux-musl")
set(CMAKE_C_FLAGS_INIT             "-target ${_target}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-target ${_target} -static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target ${_target}")
