# Linux x86_64 musl toolchain — uses `zig cc` as a drop-in Clang.
# Bootstrapped by scripts/fetch_zig.sh, which puts `zig` on PATH.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(_zig zig REQUIRED)

set(CMAKE_C_COMPILER       "${_zig}")
set(CMAKE_C_COMPILER_ARG1  "cc")
set(CMAKE_AR               "${_zig}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB           "${_zig}" CACHE FILEPATH "" FORCE)

# Zig multiplex: `zig cc -target x86_64-linux-musl`.
set(_target "x86_64-linux-musl")
foreach(_lang IN ITEMS C)
    set(CMAKE_${_lang}_FLAGS_INIT "-target ${_target}")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-target ${_target} -static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target ${_target}")

# zig cc bundles its own clang; tell CMake not to probe for ranlib/ar
# binaries that don't fit the multiplexed invocation.
set(CMAKE_C_COMPILER_AR     "${_zig};ar")
set(CMAKE_C_COMPILER_RANLIB "${_zig};ranlib")
