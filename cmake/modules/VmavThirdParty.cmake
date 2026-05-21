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
