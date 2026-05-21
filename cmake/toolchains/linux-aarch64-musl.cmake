# Linux aarch64 musl toolchain — uses `zig cc` / `zig c++` wrappers.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

find_program(_zig_cc     zig-cc     REQUIRED)
find_program(_zig_cxx    zig-c++    REQUIRED)
find_program(_zig_ar     zig-ar     REQUIRED)
find_program(_zig_ranlib zig-ranlib REQUIRED)

set(CMAKE_C_COMPILER   "${_zig_cc}")
set(CMAKE_CXX_COMPILER "${_zig_cxx}")
set(CMAKE_AR           "${_zig_ar}"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       "${_zig_ranlib}" CACHE FILEPATH "" FORCE)

set(_target "aarch64-linux-musl")
set(CMAKE_C_FLAGS_INIT             "-target ${_target}")
set(CMAKE_CXX_FLAGS_INIT           "-target ${_target}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-target ${_target} -static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target ${_target}")

# zig cc's lld wrapper rejects --dependency-file=... (CMake 3.27+
# emits it for ninja link-step dep tracking).
set(CMAKE_C_LINKER_DEPFILE_SUPPORTED   FALSE)
set(CMAKE_CXX_LINKER_DEPFILE_SUPPORTED FALSE)
