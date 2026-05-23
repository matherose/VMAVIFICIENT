# VmavThirdParty.cmake — vendored static deps orchestrator.
#
# Each dep is a git submodule under third_party/<name>/ pinned at a
# tagged release. We build them all statically, into a private
# include/link surface that consumers reach via the vmav::tp::<name>
# alias targets.
#
# This file is the single seam between vmavificient and its
# dependencies; adding a new dep means dropping a submodule and adding
# an add_subdirectory + target_alias line below.

include_guard(GLOBAL)

# === cJSON v1.7.18 ============================================
# Pure C, MIT-licensed, single .c/.h + a few helper files. Used by
# vmav_cache (Phase 3c), vmav_log JSON sink (already present), and
# Phase 3+ TMDB / probe JSON parsing.

set(_cjson_dir "${CMAKE_SOURCE_DIR}/third_party/cjson")
if(NOT EXISTS "${_cjson_dir}/cJSON.c")
    message(FATAL_ERROR
        "third_party/cjson submodule missing at ${_cjson_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

# cJSON's upstream CMakeLists.txt is heavy (test runners, format, etc.).
# We build it as a tiny private static lib directly from its sources to
# avoid pulling in cJSON's own configuration. Two .c files cover what
# we use (cJSON itself; the rest like cJSON_Utils is optional).
add_library(vmav_tp_cjson STATIC
    "${_cjson_dir}/cJSON.c")
add_library(vmav::tp::cjson ALIAS vmav_tp_cjson)

# Treat upstream headers as SYSTEM so our strict -Wpedantic / -Werror
# doesn't flag third-party stylistic choices.
target_include_directories(vmav_tp_cjson SYSTEM PUBLIC "${_cjson_dir}")

# cJSON does its own sprintf-ing with %lg/%g and assumes setlocale
# isn't in the way — fine for our use. Silence the few warnings its
# code triggers under our flags.
target_compile_options(vmav_tp_cjson PRIVATE -w)

# Hide cJSON's symbols from the final binary so multiple cJSON copies
# (ours + anything pulled in by a future dep) don't collide at link.
set_target_properties(vmav_tp_cjson PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)

message(STATUS "VmavThirdParty: cJSON v1.7.18 (vendored static)")

# === Global vendoring policy ==================================
# Every dep below is built static. We set BUILD_SHARED_LIBS off ONCE so
# upstream CMakeLists's `add_library(foo ...)` (without explicit STATIC)
# resolve to static. Saves us patching each upstream.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "vmav: all third_party deps static" FORCE)

# Suppress install/export rules from upstream — we don't ship the
# vendored deps as standalone libraries.
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

# Helper: wrap an upstream's CMakeLists in EXCLUDE_FROM_ALL so the build
# graph only pulls in deps that our targets actually link.
function(vmav_tp_add_subdir name source_dir)
    add_subdirectory("${source_dir}"
                     "${CMAKE_BINARY_DIR}/_tp/${name}"
                     EXCLUDE_FROM_ALL)
endfunction()

# Helper: ExternalProject_Add wrapper for upstreams that refuse
# add_subdirectory (libjpeg-turbo) or use autoconf (OpenSSL, FFmpeg).
# Builds the upstream as a sub-cmake at BUILD time, then exposes its
# install via an IMPORTED static target named vmav_tp_<name>.
#
# Usage:
#   vmav_tp_add_external(libjpeg-turbo "${src}"
#       STATIC_LIB "lib/libjpeg.a"
#       CMAKE_ARGS -DENABLE_SHARED=OFF -DWITH_SIMD=OFF ...
#       INCLUDE_DIRS <extra>...
#       LINK_LIBS    <other vmav::tp::* deps>)
function(vmav_tp_add_external name source_dir)
    cmake_parse_arguments(VMAV_EP
        ""
        "STATIC_LIB"
        "CMAKE_ARGS;INCLUDE_DIRS;LINK_LIBS"
        ${ARGN})
    if(NOT VMAV_EP_STATIC_LIB)
        message(FATAL_ERROR "vmav_tp_add_external(${name}): STATIC_LIB is required")
    endif()

    set(_install_dir "${CMAKE_BINARY_DIR}/_tp_install/${name}")
    set(_byproduct "${_install_dir}/${VMAV_EP_STATIC_LIB}")

    # Forward cross-compile settings to the sub-CMake invocation. The
    # toolchain file is the most important one; OSX vars matter for
    # macOS native builds; CFLAGS we set globally make their way in via
    # the toolchain file.
    set(_fwd_args "")
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _fwd_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND _fwd_args
             "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND _fwd_args
             "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(CMAKE_OSX_SYSROOT)
        list(APPEND _fwd_args "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
    endif()

    include(ExternalProject)
    ExternalProject_Add(${name}-ep
        SOURCE_DIR     "${source_dir}"
        BINARY_DIR     "${CMAKE_BINARY_DIR}/_tp_build/${name}"
        INSTALL_DIR    "${_install_dir}"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DBUILD_SHARED_LIBS=OFF
            # Force the lib dir to a simple `lib/`. On Debian-derivative
            # native builds, GNUInstallDirs puts libs into
            # `lib/x86_64-linux-gnu/` (multiarch) by default — that
            # breaks our STATIC_LIB path predictions and the
            # `lib/pkgconfig/` lookups downstream consumers (FFmpeg
            # config probing opus, etc.) do.
            -DCMAKE_INSTALL_LIBDIR=lib
            ${_fwd_args}
            ${VMAV_EP_CMAKE_ARGS}
        BUILD_BYPRODUCTS "${_byproduct}"
        UPDATE_DISCONNECTED TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD     TRUE
        LOG_INSTALL   TRUE
        LOG_OUTPUT_ON_FAILURE TRUE)

    # IMPORTED targets can't reference nonexistent dirs at config time
    # via INTERFACE_INCLUDE_DIRECTORIES — pre-create the include dir.
    file(MAKE_DIRECTORY "${_install_dir}/include")

    add_library(vmav_tp_${name} STATIC IMPORTED GLOBAL)
    add_dependencies(vmav_tp_${name} ${name}-ep)
    set_target_properties(vmav_tp_${name} PROPERTIES
        IMPORTED_LOCATION "${_byproduct}")
    target_include_directories(vmav_tp_${name} SYSTEM INTERFACE
        "${_install_dir}/include"
        ${VMAV_EP_INCLUDE_DIRS})
    if(VMAV_EP_LINK_LIBS)
        target_link_libraries(vmav_tp_${name} INTERFACE ${VMAV_EP_LINK_LIBS})
    endif()
    # Linux glibc: each C library needs the standard `-lm -lpthread
    # -ldl -lrt` system libs propagated to consumers, since glibc
    # splits these out of libc proper. macOS auto-links them via
    # libSystem; Windows MinGW already has them in the .a we ship.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(vmav_tp_${name} INTERFACE m pthread dl rt)
    endif()
endfunction()

# === zlib v1.3.1 (ExternalProject) ============================
# Foundation dep for libpng + libtiff (m2) and FFmpeg + libcurl (m3+).
# Built via ExternalProject_Add to get a stable install layout — m2's
# later deps (libtiff, libpng) need ZLIB_INCLUDE_DIR / ZLIB_LIBRARY
# pointing at known paths to skip their host-system find_package(ZLIB).

set(_zlib_dir "${CMAKE_SOURCE_DIR}/third_party/zlib")
if(NOT EXISTS "${_zlib_dir}/zlib.h")
    message(FATAL_ERROR
        "third_party/zlib submodule missing at ${_zlib_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

# zlib's CMakeLists installs the static lib under different names per
# platform: lib/libz.a on Unix, lib/libzlibstatic.a on MinGW. Resolve
# the path here so the rest of the file can reuse it.
if(WIN32)
    set(_zlib_static_rel "lib/libzlibstatic.a")
else()
    set(_zlib_static_rel "lib/libz.a")
endif()

vmav_tp_add_external(zlib "${_zlib_dir}"
    STATIC_LIB "${_zlib_static_rel}"
    CMAKE_ARGS
        -DZLIB_BUILD_EXAMPLES=OFF
        -DZLIB_BUILD_TESTING=OFF)

set(_zlib_install "${CMAKE_BINARY_DIR}/_tp_install/zlib")
set(_zlib_static_path "${_zlib_install}/${_zlib_static_rel}")
add_library(vmav::tp::zlib ALIAS vmav_tp_zlib)

message(STATUS "VmavThirdParty: zlib v1.3.1 (vendored static, ExternalProject)")

# === libpng v1.6.43 (ExternalProject) =========================
# Depends on zlib. Pass our zlib install paths so libpng's
# find_package(ZLIB) skips host lookup.

set(_libpng_dir "${CMAKE_SOURCE_DIR}/third_party/libpng")
if(NOT EXISTS "${_libpng_dir}/png.h")
    message(FATAL_ERROR
        "third_party/libpng submodule missing at ${_libpng_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

vmav_tp_add_external(libpng "${_libpng_dir}"
    STATIC_LIB "lib/libpng16.a"
    CMAKE_ARGS
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        -DPNG_FRAMEWORK=OFF
        -DPNG_TESTS=OFF
        -DPNG_TOOLS=OFF
        -DPNG_EXECUTABLES=OFF
        -DPNG_DEBUG=OFF
        # libpng ships ARM32 NEON assembly that the aarch64 assembler
        # rejects (vmovn.u16/vbsl/vadd.u8 are 32-bit ARM mnemonics).
        # Disabling all hardware optimizations is the portable fix —
        # we trade a few percent decode speed for "builds on every
        # arch we ship". Tesseract / leptonica use libpng for occasional
        # PGS-subtitle bitmaps so the perf hit is negligible.
        -DPNG_HARDWARE_OPTIMIZATIONS=OFF
        -DPNG_ARM_NEON=off
        -DPNG_INTEL_SSE=off
        -DZLIB_ROOT=${_zlib_install}
        -DZLIB_INCLUDE_DIR=${_zlib_install}/include
        -DZLIB_LIBRARY=${_zlib_static_path}
    LINK_LIBS vmav::tp::zlib)
add_dependencies(libpng-ep zlib-ep)

set(_libpng_install "${CMAKE_BINARY_DIR}/_tp_install/libpng")
add_library(vmav::tp::png ALIAS vmav_tp_libpng)

message(STATUS "VmavThirdParty: libpng v1.6.43 (vendored static, ExternalProject)")

# === libjpeg-turbo 3.0.3 (ExternalProject) ====================
# WITH_SIMD=OFF avoids the NASM (x86) / GAS (arm) build-tool dependency
# at the cost of some encode/decode speed. Tesseract / leptonica don't
# care about codec speed for our use (occasional PGS subtitle bitmaps).

set(_libjpegturbo_dir "${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo")
if(NOT EXISTS "${_libjpegturbo_dir}/jpeglib.h")
    message(FATAL_ERROR
        "third_party/libjpeg-turbo submodule missing at ${_libjpegturbo_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

vmav_tp_add_external(libjpeg-turbo "${_libjpegturbo_dir}"
    STATIC_LIB "lib/libjpeg.a"
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_SIMD=OFF
        -DWITH_TURBOJPEG=OFF
        -DWITH_JAVA=OFF
        -DWITH_FUZZ=OFF
        -DWITH_JPEG7=OFF
        -DWITH_JPEG8=OFF)

set(_libjpeg_install "${CMAKE_BINARY_DIR}/_tp_install/libjpeg-turbo")
add_library(vmav::tp::jpeg ALIAS vmav_tp_libjpeg-turbo)

message(STATUS "VmavThirdParty: libjpeg-turbo 3.0.3 (vendored static, no SIMD, ExternalProject)")

# === libtiff v4.6.0 (ExternalProject) =========================
# Depends on zlib + libjpeg via its find_package calls. Pass our
# vendored install paths so it doesn't pick up host versions.

set(_libtiff_dir "${CMAKE_SOURCE_DIR}/third_party/libtiff")
if(NOT EXISTS "${_libtiff_dir}/libtiff/tiff.h")
    message(FATAL_ERROR
        "third_party/libtiff submodule missing at ${_libtiff_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

vmav_tp_add_external(libtiff "${_libtiff_dir}"
    STATIC_LIB "lib/libtiff.a"
    CMAKE_ARGS
        -Dtiff-tools=OFF
        -Dtiff-tools-unsupported=OFF
        -Dtiff-tests=OFF
        -Dtiff-contrib=OFF
        -Dtiff-docs=OFF
        -Dtiff-deprecated=OFF
        # Explicitly disable every optional codec — otherwise libtiff's
        # CMakeLists `find_package(X)` finds host system libraries and
        # turns them on, leaving us with unresolved symbols at link
        # time. Only zlib + jpeg are enabled (both vendored).
        -Dwebp=OFF
        -Dlerc=OFF
        -Dlzma=OFF
        -Dzstd=OFF
        -Djbig=OFF
        -Dlibdeflate=OFF
        -DZLIB_ROOT=${_zlib_install}
        -DZLIB_INCLUDE_DIR=${_zlib_install}/include
        -DZLIB_LIBRARY=${_zlib_static_path}
        -DJPEG_INCLUDE_DIR=${_libjpeg_install}/include
        -DJPEG_LIBRARY=${_libjpeg_install}/lib/libjpeg.a
    LINK_LIBS vmav::tp::zlib vmav::tp::jpeg)
add_dependencies(libtiff-ep zlib-ep libjpeg-turbo-ep)

add_library(vmav::tp::tiff ALIAS vmav_tp_libtiff)

message(STATUS "VmavThirdParty: libtiff v4.6.0 (vendored static, ExternalProject)")

# === leptonica v1.84.1 (ExternalProject) ======================
# Image library used by Tesseract: provides Pix (8-bit grayscale and
# RGBA images), I/O wrappers for PNG/TIFF/JPEG, and primitives like
# pixScale / pixOtsuThreshOnBackgroundNorm that v1's subtitle_convert
# uses for OCR pre-processing. Depends on zlib + libpng + libtiff +
# libjpeg — all vendored, all pinned. Optional codecs (GIF, WebP,
# OpenJPEG) explicitly disabled to keep the dep graph closed.

set(_leptonica_dir "${CMAKE_SOURCE_DIR}/third_party/leptonica")
if(NOT EXISTS "${_leptonica_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/leptonica submodule missing at ${_leptonica_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

# Leptonica appends VERSION to its lib name on Windows but not
# elsewhere: `libleptonica-1.84.1.a` on MinGW, `libleptonica.a` on
# Linux/macOS. Same pattern as zlib (libz.a / libzlibstatic.a).
if(WIN32)
    set(_leptonica_static_rel "lib/libleptonica-1.84.1.a")
else()
    set(_leptonica_static_rel "lib/libleptonica.a")
endif()

vmav_tp_add_external(leptonica "${_leptonica_dir}"
    STATIC_LIB "${_leptonica_static_rel}"
    CMAKE_ARGS
        # leptonica 1.84.1 declares `cmake_minimum_required(VERSION 3.1.3)`.
        # CMake 4.x removed compatibility for < 3.5. Tell CMake to use
        # 3.5 policies for this subproject so configure doesn't bail.
        # (The other vendored deps either declare ≥ 3.5 or use the
        # `min...max` range form CMake 4 accepts.)
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DBUILD_PROG=OFF
        -DSW_BUILD=OFF
        # Disable optional codecs we don't vendor. Leaving these ON
        # would have leptonica's CMakeLists pick up host system libs.
        -DENABLE_GIF=OFF
        -DENABLE_WEBP=OFF
        -DENABLE_OPENJPEG=OFF
        # Vendored codec libs — same wiring as libtiff above.
        -DZLIB_ROOT=${_zlib_install}
        -DZLIB_INCLUDE_DIR=${_zlib_install}/include
        -DZLIB_LIBRARY=${_zlib_static_path}
        -DPNG_PNG_INCLUDE_DIR=${_libpng_install}/include
        -DPNG_LIBRARY=${_libpng_install}/lib/libpng16.a
        -DJPEG_INCLUDE_DIR=${_libjpeg_install}/include
        -DJPEG_LIBRARY=${_libjpeg_install}/lib/libjpeg.a
        -DTIFF_INCLUDE_DIR=${CMAKE_BINARY_DIR}/_tp_install/libtiff/include
        -DTIFF_LIBRARY=${CMAKE_BINARY_DIR}/_tp_install/libtiff/lib/libtiff.a
    LINK_LIBS vmav::tp::tiff vmav::tp::png vmav::tp::jpeg vmav::tp::zlib)
add_dependencies(leptonica-ep
    zlib-ep libpng-ep libtiff-ep libjpeg-turbo-ep)

set(_leptonica_install "${CMAKE_BINARY_DIR}/_tp_install/leptonica")
# leptonica installs its headers under include/leptonica/, but v1's
# subtitle_convert.c (and Tesseract's own build) does `#include
# <allheaders.h>` without the leptonica/ prefix. Expose both paths.
file(MAKE_DIRECTORY "${_leptonica_install}/include/leptonica")
target_include_directories(vmav_tp_leptonica SYSTEM INTERFACE
    "${_leptonica_install}/include/leptonica")
add_library(vmav::tp::leptonica ALIAS vmav_tp_leptonica)

message(STATUS "VmavThirdParty: leptonica v1.84.1 (vendored static, ExternalProject)")

# === Tesseract 5.5.2 (ExternalProject) ========================
# C++17 OCR engine. Uses leptonica for image I/O + pre-processing.
# Tesseract's CMake build is well-behaved when given its deps via the
# standard `find_package(Leptonica CONFIG)` mechanism — leptonica's
# install includes a cmake/leptonica/LeptonicaConfig.cmake. We point
# Tesseract's CMAKE_PREFIX_PATH at the leptonica install dir and
# disable every optional feature we don't need (training tools,
# OpenMP, libarchive, libcurl).

set(_tesseract_dir "${CMAKE_SOURCE_DIR}/third_party/tesseract")
if(NOT EXISTS "${_tesseract_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/tesseract submodule missing at ${_tesseract_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

# Tesseract appends MAJORMINOR to its lib name on Windows but not
# elsewhere: `libtesseract55.a` on MinGW, `libtesseract.a` on
# Linux/macOS.
if(WIN32)
    set(_tesseract_static_rel "lib/libtesseract55.a")
else()
    set(_tesseract_static_rel "lib/libtesseract.a")
endif()

vmav_tp_add_external(tesseract "${_tesseract_dir}"
    STATIC_LIB "${_tesseract_static_rel}"
    CMAKE_ARGS
        # Tesseract's CMake builds + tests + training off.
        -DBUILD_TRAINING_TOOLS=OFF
        -DBUILD_TESTS=OFF
        -DINSTALL_CONFIGS=OFF
        # Disable optional deps. We don't ship libarchive / libcurl /
        # OpenMP / ICU; auto-detection would otherwise pick up host
        # libs and break the static link plan.
        -DDISABLE_ARCHIVE=ON
        -DDISABLE_CURL=ON
        -DOPENMP_BUILD=OFF
        -DUSE_SYSTEM_ICU=OFF
        -DSW_BUILD=OFF
        # Disable Tesseract's direct libtiff codepath. Our subtitle
        # pipeline OCRs in-memory PIX buffers (PGS bitmaps → leptonica
        # PIX → TessBaseAPISetImage2), never TIFF files.
        -DDISABLE_TIFF=ON
        # Tesseract runs `check_leptonica_tiff_support()` (a try_run
        # probe) UNCONDITIONALLY before DISABLE_TIFF gates anything.
        # CMake reports try_run as cross-compile mode the moment
        # CMAKE_OSX_ARCHITECTURES / CMAKE_TOOLCHAIN_FILE is set —
        # which is every job we run, native or not. Pre-set the
        # cache result so CMake skips the probe entirely. "0" means
        # the probe succeeded → "leptonica has TIFF". Combined with
        # DISABLE_TIFF=ON above, the (NOT LEPT_TIFF_RESULT EQUAL 0)
        # branch never fires and Tesseract simply notes leptonica
        # has TIFF and leaves DISABLE_TIFF alone.
        -DLEPT_TIFF_RESULT=0
        # Disable AVX-512 dotproduct path. Tesseract's CMake probes
        # `-mavx512f` via check_cxx_compiler_flag — Clang ACCEPTS the
        # flag, so the probe says yes, but its AVX-512 intrinsics also
        # need `-mevex512` (a Clang 15+ split) which isn't propagated
        # to dotproductavx512.cpp's COMPILE_FLAGS. The file then errors
        # with "_mm512_setzero_ps requires target feature 'evex512'".
        # CheckCompilerFlag.cmake early-returns when the result var is
        # already in the cache, so pre-setting HAVE_AVX512F:BOOL=OFF
        # skips the probe and drops the file from the source list. The
        # AVX2/FMA/SSE4.1 paths still light up; we just don't get the
        # 512-bit SIMD — which is irrelevant for subtitle-bitmap OCR.
        -DHAVE_AVX512F:BOOL=OFF
        # Tesseract finds leptonica via Leptonica_DIR pointing at the
        # exported CMake config (lib/cmake/leptonica/Leptonica*.cmake).
        # Leptonica's config carries the transitive deps it needs
        # (libpng, libjpeg) via INTERFACE_LINK_LIBRARIES, so we don't
        # need a multi-path CMAKE_PREFIX_PATH (which got mangled by
        # ExternalProject's argument splitting at `;`).
        -DLeptonica_DIR=${_leptonica_install}/lib/cmake/leptonica
    LINK_LIBS vmav::tp::leptonica vmav::tp::tiff vmav::tp::png
              vmav::tp::jpeg vmav::tp::zlib)
add_dependencies(tesseract-ep
    leptonica-ep libtiff-ep libpng-ep libjpeg-turbo-ep zlib-ep)

set(_tesseract_install "${CMAKE_BINARY_DIR}/_tp_install/tesseract")
# Tesseract is C++; force CMake to use the C++ linker driver on any
# binary that consumes vmav_tp_tesseract. Without this, our C-only
# binaries try to link with `cc` and fail to resolve libc++/libstdc++
# symbols (std::ios_base, std::locale, std::ctype, …).
set_property(TARGET vmav_tp_tesseract
    PROPERTY IMPORTED_LINK_INTERFACE_LANGUAGES "CXX")
# On Windows, Tesseract calls into Winsock (send/recv/socket/
# WSAStartup/...) for its network I/O paths; the static archive
# references those symbols with __declspec(dllimport), so consumers
# need -lws2_32 in their link line. Same propagation pattern as
# libdovi/libhdr10plus on Windows.
if(WIN32)
    target_link_libraries(vmav_tp_tesseract INTERFACE ws2_32)
endif()
add_library(vmav::tp::tesseract ALIAS vmav_tp_tesseract)

message(STATUS "VmavThirdParty: Tesseract 5.5.2 (vendored static, ExternalProject)")

# === SVT-AV1-HDR v4.1.0 (juliobbv-p fork, ExternalProject) ====
# AV1 encoder used for the actual video encode. We pin the juliobbv-p
# HDR fork (not upstream SVT-AV1) because Phase 4's encoder needs the
# fork's --enable-hdr (HDR10/HDR10+) and Dolby Vision RPU passthrough
# paths that upstream doesn't carry — those are why v1 always required
# the fork. CMake build, no extra deps beyond libc/libpthread.
#
# Built as a static archive; consumers link against vmav::tp::svtav1
# and get the SvtAv1Enc.h C API header on their include path.

set(_svtav1_dir "${CMAKE_SOURCE_DIR}/third_party/svt-av1-hdr")
if(NOT EXISTS "${_svtav1_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/svt-av1-hdr submodule missing at ${_svtav1_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

vmav_tp_add_external(svtav1 "${_svtav1_dir}"
    STATIC_LIB "lib/libSvtAv1Enc.a"
    CMAKE_ARGS
        # We need the encoder library only — skip the CLI app + tests.
        -DBUILD_APPS=OFF
        -DBUILD_TESTING=OFF
        # SVT-AV1's NATIVE_BUILD flag enables -march=native. Off for
        # deterministic builds; per-CPU-feature dispatch in SVT-AV1
        # picks the right kernels at runtime regardless.
        -DREPRODUCIBLE_BUILDS=ON
        -DNATIVE=OFF)

set(_svtav1_install "${CMAKE_BINARY_DIR}/_tp_install/svtav1")
# Headers install under include/svt-av1/ — expose that as an extra
# INTERFACE include path so consumers can `#include <EbSvtAv1Enc.h>`
# without the `svt-av1/` prefix (matches v1's source).
file(MAKE_DIRECTORY "${_svtav1_install}/include/svt-av1")
target_include_directories(vmav_tp_svtav1 SYSTEM INTERFACE
    "${_svtav1_install}/include/svt-av1")
add_library(vmav::tp::svtav1 ALIAS vmav_tp_svtav1)

message(STATUS "VmavThirdParty: SVT-AV1-HDR v4.1.0 (juliobbv-p, vendored static, ExternalProject)")

# === OpenSSL 3.3.7 LTS (autoconf-style ExternalProject) =======
# OpenSSL uses its own Perl-based Configure script, not CMake. Wire it
# up via a custom ExternalProject CONFIGURE_COMMAND. Each platform has
# its own OpenSSL "target name" recognized by Configure.
#
# We synthesize a tiny CC-wrapper script so the cross-compile -target
# flag (currently lives in CMAKE_C_FLAGS_INIT) is baked into the
# compiler invocation OpenSSL sees. OpenSSL's `CC=foo bar baz` env-var
# parsing is fragile across CMake's quoting layers, so the wrapper is
# the cleanest path.

set(_openssl_dir "${CMAKE_SOURCE_DIR}/third_party/openssl")
if(NOT EXISTS "${_openssl_dir}/Configure")
    message(FATAL_ERROR
        "third_party/openssl submodule missing at ${_openssl_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

# Per-platform Configure target.
if(WIN32)
    set(_openssl_target "mingw64")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64"
       OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_openssl_target "darwin64-arm64-cc")
    else()
        set(_openssl_target "darwin64-x86_64-cc")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(_openssl_target "linux-aarch64")
    else()
        set(_openssl_target "linux-x86_64-clang")
    endif()
else()
    message(FATAL_ERROR "OpenSSL: unsupported target ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(_openssl_install "${CMAKE_BINARY_DIR}/_tp_install/openssl")
set(_openssl_build "${CMAKE_BINARY_DIR}/_tp_build/openssl")
file(MAKE_DIRECTORY "${_openssl_build}")
file(MAKE_DIRECTORY "${_openssl_install}/include")

# CC wrapper that bakes in every compiler flag CMake would normally
# inject implicitly — cross-compile -target, macOS -isysroot, etc.
# OpenSSL's Configure invokes CC directly via its own Makefile so we
# can't rely on CMake's per-invocation flag construction.
set(_openssl_cc_flags "${CMAKE_C_FLAGS_INIT}")
if(APPLE)
    if(CMAKE_OSX_SYSROOT)
        set(_openssl_cc_flags "${_openssl_cc_flags} -isysroot ${CMAKE_OSX_SYSROOT}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(_openssl_cc_flags
            "${_openssl_cc_flags} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endif()

set(_openssl_cc "${_openssl_build}/cc-wrapper.sh")
file(WRITE "${_openssl_cc}"
"#!/bin/sh\nexec \"${CMAKE_C_COMPILER}\" ${_openssl_cc_flags} \"$@\"\n")
execute_process(COMMAND chmod +x "${_openssl_cc}")

include(ExternalProject)
ExternalProject_Add(openssl-ep
    SOURCE_DIR  "${_openssl_dir}"
    BINARY_DIR  "${_openssl_build}"
    INSTALL_DIR "${_openssl_install}"
    CONFIGURE_COMMAND
        perl "${_openssl_dir}/Configure" "${_openssl_target}"
            --prefix=<INSTALL_DIR>
            --libdir=lib
            --openssldir=<INSTALL_DIR>/ssl
            "CC=${_openssl_cc}"
            "AR=${CMAKE_AR}"
            "RANLIB=${CMAKE_RANLIB}"
            no-shared
            no-tests
            no-docs
            no-apps
            no-engine
            # no-module skips dynamic-loadable providers (the legacy.so
            # / legacy.dylib files); without it, install_sw rebuilds them
            # and links against KDF symbols our static config omits.
            no-module
            no-legacy
            no-dynamic-engine
    BUILD_COMMAND
        make -j build_libs
    INSTALL_COMMAND
        make install_dev
    BUILD_BYPRODUCTS
        "${_openssl_install}/lib/libssl.a"
        "${_openssl_install}/lib/libcrypto.a"
    UPDATE_DISCONNECTED TRUE
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
    LOG_OUTPUT_ON_FAILURE TRUE)

# Expose two IMPORTED targets — libssl depends on libcrypto, and on
# Windows both need link-time winsock + crypt32.
foreach(_lib crypto ssl)
    add_library(vmav_tp_${_lib} STATIC IMPORTED GLOBAL)
    add_dependencies(vmav_tp_${_lib} openssl-ep)
    set_target_properties(vmav_tp_${_lib} PROPERTIES
        IMPORTED_LOCATION "${_openssl_install}/lib/lib${_lib}.a")
    target_include_directories(vmav_tp_${_lib} SYSTEM INTERFACE
        "${_openssl_install}/include")
endforeach()

target_link_libraries(vmav_tp_ssl INTERFACE vmav_tp_crypto)

if(WIN32)
    target_link_libraries(vmav_tp_crypto INTERFACE ws2_32 crypt32)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(vmav_tp_crypto INTERFACE pthread dl)
endif()

add_library(vmav::tp::ssl    ALIAS vmav_tp_ssl)
add_library(vmav::tp::crypto ALIAS vmav_tp_crypto)

message(STATUS "VmavThirdParty: OpenSSL 3.3.7 LTS (target ${_openssl_target}, ExternalProject)")

# === libcurl 8.7.1 (CMake ExternalProject) ====================
# HTTPS client used by tmdb_client. CMake build; trivial compared to
# OpenSSL once the OPENSSL_* find_package vars point at our vendored
# install. Disable every protocol/feature we don't need so the static
# binary stays small and we avoid hidden host-deps.

set(_curl_dir "${CMAKE_SOURCE_DIR}/third_party/curl")
if(NOT EXISTS "${_curl_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/curl submodule missing at ${_curl_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

vmav_tp_add_external(curl "${_curl_dir}"
    STATIC_LIB "lib/libcurl.a"
    CMAKE_ARGS
        -DBUILD_CURL_EXE=OFF
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_LIBCURL_DOCS=OFF
        -DBUILD_MISC_DOCS=OFF
        -DENABLE_CURL_MANUAL=OFF
        -DBUILD_TESTING=OFF
        # TLS backend: our vendored OpenSSL.
        -DCURL_USE_OPENSSL=ON
        -DOPENSSL_ROOT_DIR=${_openssl_install}
        -DOPENSSL_INCLUDE_DIR=${_openssl_install}/include
        -DOPENSSL_SSL_LIBRARY=${_openssl_install}/lib/libssl.a
        -DOPENSSL_CRYPTO_LIBRARY=${_openssl_install}/lib/libcrypto.a
        # Compression: our vendored zlib.
        -DCURL_ZLIB=ON
        -DZLIB_ROOT=${_zlib_install}
        -DZLIB_INCLUDE_DIR=${_zlib_install}/include
        -DZLIB_LIBRARY=${_zlib_static_path}
        # Disable everything we don't need — keeps the .a small and
        # eliminates auto-find_package for host system libs.
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
        -DCURL_DISABLE_GOPHER=ON
        -DCURL_DISABLE_TELNET=ON
        -DCURL_DISABLE_DICT=ON
        -DCURL_DISABLE_FILE=ON
        -DCURL_DISABLE_TFTP=ON
        -DCURL_DISABLE_RTSP=ON
        -DCURL_DISABLE_POP3=ON
        -DCURL_DISABLE_IMAP=ON
        -DCURL_DISABLE_SMTP=ON
        -DCURL_DISABLE_SMB=ON
        -DCURL_DISABLE_MQTT=ON
        -DCURL_DISABLE_FTP=ON
        -DUSE_LIBIDN2=OFF
        -DUSE_NGHTTP2=OFF
        -DCURL_USE_LIBPSL=OFF
        -DCURL_USE_LIBSSH2=OFF
        -DCURL_USE_LIBSSH=OFF
        -DCURL_USE_GSSAPI=OFF
        -DCURL_BROTLI=OFF
        -DCURL_ZSTD=OFF
    LINK_LIBS vmav::tp::ssl vmav::tp::crypto vmav::tp::zlib)
add_dependencies(curl-ep openssl-ep zlib-ep)

set(_curl_install "${CMAKE_BINARY_DIR}/_tp_install/curl")

if(WIN32)
    # libcurl on Windows needs winsock + crypt32. With LDAP disabled we
    # don't need wldap32.
    target_link_libraries(vmav_tp_curl INTERFACE ws2_32 crypt32 bcrypt)
    # libcurl's headers default to __declspec(dllimport); consumers
    # building against the static .a must define CURL_STATICLIB so the
    # extern decls drop the import attribute.
    target_compile_definitions(vmav_tp_curl INTERFACE CURL_STATICLIB)
elseif(APPLE)
    # libcurl on macOS pulls in CoreFoundation + SystemConfiguration
    # for the macOS-native proxy resolver (Curl_macos_init reads
    # SCDynamicStoreCopyProxies).
    target_link_libraries(vmav_tp_curl INTERFACE
        "-framework CoreFoundation"
        "-framework SystemConfiguration")
endif()

add_library(vmav::tp::curl ALIAS vmav_tp_curl)

message(STATUS "VmavThirdParty: libcurl 8.7.1 (vendored static, ExternalProject)")

# === libopus 1.5.2 (CMake ExternalProject) ====================
# Audio codec for the .opus output tracks. Built before FFmpeg so
# FFmpeg's configure can pick it up via --enable-libopus and emit the
# `libopus` encoder/decoder + `opus` muxer. Audio code links lavc
# (not libopus directly), so we don't expose vmav::tp::opus to the
# rest of the tree — but the static lib must exist on disk when
# FFmpeg's configure probe runs.

set(_opus_dir "${CMAKE_SOURCE_DIR}/third_party/opus")
if(NOT EXISTS "${_opus_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/opus submodule missing at ${_opus_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

vmav_tp_add_external(opus "${_opus_dir}"
    STATIC_LIB "lib/libopus.a"
    CMAKE_ARGS
        -DOPUS_BUILD_SHARED_LIBRARY=OFF
        -DOPUS_BUILD_TESTING=OFF
        -DOPUS_BUILD_PROGRAMS=OFF
        -DOPUS_INSTALL_PKG_CONFIG_MODULE=ON
        -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF)

set(_opus_install "${CMAKE_BINARY_DIR}/_tp_install/opus")
add_library(vmav::tp::opus ALIAS vmav_tp_opus)

message(STATUS "VmavThirdParty: libopus 1.5.2 (vendored static, ExternalProject)")

# === FFmpeg n8.1.1 (autoconf-style ExternalProject) ===========
# The biggest dep by far: ~3000 source files producing 6 static libs.
# Builds via FFmpeg's own ./configure (Bourne shell, not autoconf
# proper). Each platform needs target-os/arch flags. We reuse the
# OpenSSL-style CC wrapper pattern to bake in cross-compile flags
# the configure-time tiny test programs need.
#
# Output libs (in install/lib/):
#   libavformat.a, libavcodec.a, libavutil.a,
#   libavfilter.a, libswscale.a, libswresample.a

set(_ffmpeg_dir "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")
if(NOT EXISTS "${_ffmpeg_dir}/configure")
    message(FATAL_ERROR
        "third_party/ffmpeg submodule missing at ${_ffmpeg_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

# Per-platform target-os / arch / cross-prefix.
if(WIN32)
    set(_ffmpeg_target_os "mingw32")
    set(_ffmpeg_arch "x86_64")
elseif(APPLE)
    set(_ffmpeg_target_os "darwin")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64"
       OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_ffmpeg_arch "arm64")
    else()
        set(_ffmpeg_arch "x86_64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_ffmpeg_target_os "linux")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(_ffmpeg_arch "aarch64")
    else()
        set(_ffmpeg_arch "x86_64")
    endif()
else()
    message(FATAL_ERROR "FFmpeg: unsupported target ${CMAKE_SYSTEM_NAME}")
endif()

set(_ffmpeg_install "${CMAKE_BINARY_DIR}/_tp_install/ffmpeg")
set(_ffmpeg_build "${CMAKE_BINARY_DIR}/_tp_build/ffmpeg")
file(MAKE_DIRECTORY "${_ffmpeg_build}")
file(MAKE_DIRECTORY "${_ffmpeg_install}/include")

# CC wrapper baking in cross-compile + macOS sysroot flags. FFmpeg's
# configure invokes CC many times for capability probes; the wrapper
# ensures every invocation gets the right target.
set(_ffmpeg_cc_flags "${CMAKE_C_FLAGS_INIT}")
if(APPLE)
    if(CMAKE_OSX_SYSROOT)
        set(_ffmpeg_cc_flags "${_ffmpeg_cc_flags} -isysroot ${CMAKE_OSX_SYSROOT}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(_ffmpeg_cc_flags
            "${_ffmpeg_cc_flags} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endif()

set(_ffmpeg_cc "${_ffmpeg_build}/cc-wrapper.sh")
file(WRITE "${_ffmpeg_cc}"
"#!/bin/sh\nexec \"${CMAKE_C_COMPILER}\" ${_ffmpeg_cc_flags} \"$@\"\n")
execute_process(COMMAND chmod +x "${_ffmpeg_cc}")

# FFmpeg's configure runs tiny probe programs that #include <opus.h>
# and try to link against `-lopus`; bake the libopus install paths
# into extra-cflags / extra-ldflags so those probes succeed.
set(_ffmpeg_extra_cflags "-I${_opus_install}/include -I${_opus_install}/include/opus")
set(_ffmpeg_extra_ldflags "-L${_opus_install}/lib")

# Whether to allow x86 assembly. zig cc doesn't bundle nasm, so we
# disable x86asm on x86_64 targets. ARM/aarch64 GAS-style asm is
# handled by clang's integrated assembler — keep that path on.
set(_ffmpeg_asm_args "")
if(_ffmpeg_arch STREQUAL "x86_64")
    list(APPEND _ffmpeg_asm_args "--disable-x86asm" "--disable-asm")
endif()

include(ExternalProject)
ExternalProject_Add(ffmpeg-ep
    SOURCE_DIR  "${_ffmpeg_dir}"
    BINARY_DIR  "${_ffmpeg_build}"
    INSTALL_DIR "${_ffmpeg_install}"
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env "PKG_CONFIG_LIBDIR=${_opus_install}/lib/pkgconfig"
        "${_ffmpeg_dir}/configure"
            --prefix=<INSTALL_DIR>
            --enable-cross-compile
            "--target-os=${_ffmpeg_target_os}"
            "--arch=${_ffmpeg_arch}"
            "--cc=${_ffmpeg_cc}"
            "--ar=${CMAKE_AR}"
            "--ranlib=${CMAKE_RANLIB}"
            # Static binary distribution gating.
            --enable-static
            --disable-shared
            --enable-gpl
            --enable-version3
            # Trim everything we don't need.
            --disable-programs
            --disable-doc
            --disable-htmlpages
            --disable-manpages
            --disable-podpages
            --disable-txtpages
            --disable-debug
            --disable-network
            # Don't auto-detect host system libs (libxml2, libfreetype,
            # etc.) — we want a deterministic set of inputs.
            --disable-autodetect
            ${_ffmpeg_asm_args}
            # libopus: encoder + decoder + ogg muxer/demuxer for the .opus
            # output files. The audio code uses lavc's `libopus` encoder
            # (not libopus directly), so we just need FFmpeg to see it.
            # FFmpeg's libopus detection is pkg-config-only, so we keep
            # pkg-config enabled but PKG_CONFIG_LIBDIR (set in the env
            # below) constrains it to look ONLY at our opus install —
            # no host system lib leakage.
            --enable-libopus
            "--extra-cflags=${_ffmpeg_extra_cflags}"
            "--extra-ldflags=${_ffmpeg_extra_ldflags}"
            "--extra-libs=-lopus"
    # `make -j` (unbounded) on GHA's macos-14 runner spawns ~3000
    # compile processes for FFmpeg's source tree and trips
    # posix_spawn: EAGAIN under the runner's per-user process limit.
    # `-j4` is enough parallelism to be fast on every CI runner
    # we use (Linux ubuntu = 4 cores, macos-14 ~3-4 cores) without
    # exhausting the limit.
    BUILD_COMMAND
        make -j4
    INSTALL_COMMAND
        make install
    BUILD_BYPRODUCTS
        "${_ffmpeg_install}/lib/libavformat.a"
        "${_ffmpeg_install}/lib/libavcodec.a"
        "${_ffmpeg_install}/lib/libavutil.a"
        "${_ffmpeg_install}/lib/libavfilter.a"
        "${_ffmpeg_install}/lib/libswscale.a"
        "${_ffmpeg_install}/lib/libswresample.a"
    UPDATE_DISCONNECTED TRUE
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
    DEPENDS opus-ep)

# Expose the six FFmpeg libs as IMPORTED targets. Order matters at
# link time: avformat depends on avcodec, avcodec on avutil, etc.
# We capture the dep DAG via target_link_libraries(... INTERFACE ...)
# so consumers only need to link the top-level libs they use.
foreach(_lib avutil swresample swscale avcodec avformat avfilter)
    add_library(vmav_tp_${_lib} STATIC IMPORTED GLOBAL)
    add_dependencies(vmav_tp_${_lib} ffmpeg-ep)
    set_target_properties(vmav_tp_${_lib} PROPERTIES
        IMPORTED_LOCATION "${_ffmpeg_install}/lib/lib${_lib}.a")
    target_include_directories(vmav_tp_${_lib} SYSTEM INTERFACE
        "${_ffmpeg_install}/include")
endforeach()

# Dep DAG. Lower libs come last so when the linker resolves the
# upper libs' references it finds them.
target_link_libraries(vmav_tp_avformat INTERFACE
    vmav_tp_avcodec vmav_tp_swresample vmav_tp_avutil)
target_link_libraries(vmav_tp_avfilter INTERFACE
    vmav_tp_avformat vmav_tp_avcodec vmav_tp_swresample vmav_tp_swscale vmav_tp_avutil)
target_link_libraries(vmav_tp_avcodec INTERFACE
    vmav_tp_swresample vmav_tp_avutil vmav_tp_opus)
target_link_libraries(vmav_tp_swresample INTERFACE vmav_tp_avutil)
target_link_libraries(vmav_tp_swscale INTERFACE vmav_tp_avutil)

# Platform link-time system libs.
if(UNIX AND NOT APPLE)
    target_link_libraries(vmav_tp_avutil INTERFACE m pthread dl rt)
elseif(APPLE)
    target_link_libraries(vmav_tp_avutil INTERFACE
        "-framework CoreFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework AudioToolbox"
        "-framework Security")
elseif(WIN32)
    target_link_libraries(vmav_tp_avutil INTERFACE bcrypt ws2_32 secur32)
endif()

add_library(vmav::tp::avformat    ALIAS vmav_tp_avformat)
add_library(vmav::tp::avcodec     ALIAS vmav_tp_avcodec)
add_library(vmav::tp::avutil      ALIAS vmav_tp_avutil)
add_library(vmav::tp::avfilter    ALIAS vmav_tp_avfilter)
add_library(vmav::tp::swscale     ALIAS vmav_tp_swscale)
add_library(vmav::tp::swresample  ALIAS vmav_tp_swresample)

message(STATUS "VmavThirdParty: FFmpeg n8.1.1 (target ${_ffmpeg_target_os}/${_ffmpeg_arch}, ExternalProject)")

# === Rust deps (libdovi, libhdr10plus) via cargo-c ============
# Both crates produce C-callable static libs via the cargo-c subcommand.
# cargo-c handles header generation (cbindgen) + pkg-config + install.
# Each platform needs a matching rustup target installed.

# Pick the Rust target triple matching our CMAKE_SYSTEM_NAME / processor.
if(WIN32)
    set(_rust_target "x86_64-pc-windows-gnu")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64"
       OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_rust_target "aarch64-apple-darwin")
    else()
        set(_rust_target "x86_64-apple-darwin")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(_rust_target "aarch64-unknown-linux-gnu")
    else()
        set(_rust_target "x86_64-unknown-linux-gnu")
    endif()
else()
    message(FATAL_ERROR "Rust deps: unsupported target ${CMAKE_SYSTEM_NAME}")
endif()

# Helper: wrap a cargo-c crate as an ExternalProject. The crate must
# have a `capi` feature (libdovi + libhdr10plus both do). Produces an
# install layout at <INSTALL_DIR>/{lib,include}.
#
# Usage:
#   vmav_tp_add_cargo_c(libdovi
#       SOURCE_DIR ${_dovi_dir}
#       CRATE_DIR  dolby_vision
#       STATIC_LIB lib/libdovi.a
#       INCLUDE_DIR include/libdovi)
function(vmav_tp_add_cargo_c name)
    cmake_parse_arguments(VMAV_RUST
        ""
        "SOURCE_DIR;CRATE_DIR;STATIC_LIB;FEATURES"
        ""
        ${ARGN})

    set(_install_dir "${CMAKE_BINARY_DIR}/_tp_install/${name}")
    set(_crate_path "${VMAV_RUST_SOURCE_DIR}/${VMAV_RUST_CRATE_DIR}")
    set(_byproduct "${_install_dir}/${VMAV_RUST_STATIC_LIB}")
    file(MAKE_DIRECTORY "${_install_dir}/include")

    # Optional --features=X,Y. libdovi has capi as a default feature so
    # it omits this; libhdr10plus's capi is opt-in.
    set(_features_arg "")
    if(VMAV_RUST_FEATURES)
        set(_features_arg "--features=${VMAV_RUST_FEATURES}")
    endif()

    # cargo cinstall installs into <prefix>/lib + <prefix>/include.
    # --library-type=staticlib forces just the .a (no .so/.dylib).
    # --target picks the rustup triple.
    include(ExternalProject)
    ExternalProject_Add(${name}-ep
        SOURCE_DIR        "${_crate_path}"
        BINARY_DIR        "${_crate_path}"
        INSTALL_DIR       "${_install_dir}"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     ""
        INSTALL_COMMAND
            cargo cinstall
                --release
                --library-type=staticlib
                --target=${_rust_target}
                ${_features_arg}
                --prefix=<INSTALL_DIR>
                --pkgconfigdir=<INSTALL_DIR>/lib/pkgconfig
        BUILD_BYPRODUCTS  "${_byproduct}"
        UPDATE_DISCONNECTED TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE)

    add_library(vmav_tp_${name} STATIC IMPORTED GLOBAL)
    add_dependencies(vmav_tp_${name} ${name}-ep)
    set_target_properties(vmav_tp_${name} PROPERTIES
        IMPORTED_LOCATION "${_byproduct}")
    target_include_directories(vmav_tp_${name} SYSTEM INTERFACE
        "${_install_dir}/include")

    # Rust's libstd pulls in platform-specific syscall surface that
    # isn't included in the staticlib archive itself. Each target needs
    # the OS-level libs the Rust runtime depends on:
    #   * Linux musl: libunwind (no libgcc_s on musl) + libc/libm/libpthread/libdl
    #   * macOS:      iconv + libSystem (linker resolves these implicitly,
    #                 but cargo-c's pkg-config still lists them)
    #   * Windows GNU: Win32 surface used by Rust std (net/crypto/RNG/etc.)
    # zig cc and llvm-mingw both ship the relevant import libs so a bare
    # `-lws2_32` etc. resolves at link time.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(vmav_tp_${name} INTERFACE
            unwind pthread dl m c)
    elseif(WIN32)
        target_link_libraries(vmav_tp_${name} INTERFACE
            ws2_32 ntdll userenv bcrypt advapi32 cfgmgr32
            credui crypt32 cryptnet dbghelp iphlpapi kernel32
            mswsock ncrypt ole32 oleaut32 powrprof psapi
            rpcrt4 secur32 shell32 synchronization user32
            uuid winhttp wininet winmm winspool wldap32)
    endif()
    # macOS: nothing extra — libSystem + libc are linked by the driver.
endfunction()

# === libdovi 3.3.2 ============================================
# Reads + writes Dolby Vision RPU metadata. Used by rpu_extract to
# pull the RPU sidecar from a DV-tagged input video.
set(_dovi_dir "${CMAKE_SOURCE_DIR}/third_party/dovi_tool")
if(NOT EXISTS "${_dovi_dir}/dolby_vision/Cargo.toml")
    message(FATAL_ERROR
        "third_party/dovi_tool submodule missing at ${_dovi_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()
vmav_tp_add_cargo_c(libdovi
    SOURCE_DIR  "${_dovi_dir}"
    CRATE_DIR   dolby_vision
    STATIC_LIB  lib/libdovi.a)
add_library(vmav::tp::dovi ALIAS vmav_tp_libdovi)

message(STATUS "VmavThirdParty: libdovi 3.3.2 (cargo-c, target ${_rust_target})")

# === libhdr10plus 2.1.5 =======================================
# Reads + writes HDR10+ dynamic metadata. Used by rpu_extract when
# the input is HDR10+ tagged (DV is handled via libdovi).
# Crate output name is `hdr10plus-rs` per its Cargo.toml metadata.capi
# block, so the staticlib lives at lib/libhdr10plus-rs.a.
set(_hdr10p_dir "${CMAKE_SOURCE_DIR}/third_party/hdr10plus_tool")
if(NOT EXISTS "${_hdr10p_dir}/hdr10plus/Cargo.toml")
    message(FATAL_ERROR
        "third_party/hdr10plus_tool submodule missing at ${_hdr10p_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()
vmav_tp_add_cargo_c(libhdr10plus
    SOURCE_DIR  "${_hdr10p_dir}"
    CRATE_DIR   hdr10plus
    STATIC_LIB  lib/libhdr10plus-rs.a
    FEATURES    capi)
add_library(vmav::tp::hdr10plus ALIAS vmav_tp_libhdr10plus)

message(STATUS "VmavThirdParty: libhdr10plus 2.1.5 (cargo-c, target ${_rust_target})")
