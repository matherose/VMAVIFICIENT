# Windows x86_64 cross toolchain — llvm-mingw (LLVM + MinGW-w64 CRT).
# Bootstrapped by scripts/fetch_llvm_mingw.sh, which exposes the
# x86_64-w64-mingw32-clang triple on PATH.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple "x86_64-w64-mingw32")
find_program(_clang   "${_triple}-clang"    REQUIRED)
find_program(_clangxx "${_triple}-clang++"  REQUIRED)
find_program(_ar      "${_triple}-ar"       REQUIRED)
find_program(_ranlib  "${_triple}-ranlib"   REQUIRED)
find_program(_windres "${_triple}-windres")

set(CMAKE_C_COMPILER   "${_clang}")
# CXX needed when third_party deps enable LANGUAGES CXX in project()
# (libtiff, leptonica, tesseract). Without this CMake falls back to
# the host's c++ which can't cross-target Windows.
set(CMAKE_CXX_COMPILER "${_clangxx}")
set(CMAKE_AR           "${_ar}"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       "${_ranlib}" CACHE FILEPATH "" FORCE)
if(_windres)
    # CMAKE_RC_COMPILER is recorded for later enable_language(RC) at the
    # top-level CMakeLists. Calling enable_language() here would fire
    # before CMake has resolved CMAKE_MAKE_PROGRAM and fail.
    set(CMAKE_RC_COMPILER "${_windres}")
endif()

# Force static CRT + winpthread.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-static -static-libgcc -Wl,-Bstatic -lpthread")

# MinGW's <cmath> hides M_PI / M_E / etc. unless _USE_MATH_DEFINES is
# defined before the header is pulled in. Tesseract's CMakeLists only
# sets this for MSVC; we need it for our llvm-mingw builds too. Setting
# it via the toolchain ensures every vendored C++ dep built with this
# toolchain (Tesseract today, anything else later) sees the constants.
set(CMAKE_CXX_FLAGS_INIT "-D_USE_MATH_DEFINES")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Auto-detect wine for ctest. When present, CTest will prefix each test
# command with `wine` so .exe test runners execute under wine on Linux.
# Locally on macOS where wine is typically missing, we don't run tests
# anyway (build-only validation).
find_program(_wine NAMES wine wine64)
if(_wine)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${_wine}")
endif()
