# Linux aarch64 musl toolchain — uses `zig cc` as a drop-in Clang.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

find_program(_zig zig REQUIRED)

set(CMAKE_C_COMPILER       "${_zig}")
set(CMAKE_C_COMPILER_ARG1  "cc")
set(CMAKE_AR               "${_zig}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB           "${_zig}" CACHE FILEPATH "" FORCE)

set(_target "aarch64-linux-musl")
set(CMAKE_C_FLAGS_INIT             "-target ${_target}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-target ${_target} -static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target ${_target}")

set(CMAKE_C_COMPILER_AR     "${_zig};ar")
set(CMAKE_C_COMPILER_RANLIB "${_zig};ranlib")
