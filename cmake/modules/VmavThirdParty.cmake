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
