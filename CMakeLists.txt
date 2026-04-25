cmake_minimum_required(VERSION 3.24)

# ---------------------------------------------------------------------------
# Force LLVM/Clang toolchain (must be set before project()).
# Prefers Homebrew LLVM if present, then /usr/local/opt/llvm, then PATH clang.
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_C_COMPILER)
    find_program(LLVM_CLANG clang
        HINTS
            /opt/homebrew/opt/llvm/bin
            /usr/local/opt/llvm/bin
            ENV LLVM_PREFIX
        NO_DEFAULT_PATH
    )
    if(NOT LLVM_CLANG)
        find_program(LLVM_CLANG clang REQUIRED)
    endif()
    set(CMAKE_C_COMPILER "${LLVM_CLANG}" CACHE FILEPATH "C compiler" FORCE)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(LLVM_CLANGXX clang++
        HINTS
            /opt/homebrew/opt/llvm/bin
            /usr/local/opt/llvm/bin
            ENV LLVM_PREFIX
        NO_DEFAULT_PATH
    )
    if(NOT LLVM_CLANGXX)
        find_program(LLVM_CLANGXX clang++ REQUIRED)
    endif()
    set(CMAKE_CXX_COMPILER "${LLVM_CLANGXX}" CACHE FILEPATH "C++ compiler" FORCE)
endif()

# C++ is enabled purely so CMake picks the C++ driver when linking the final
# executable (tesseract is C++ and drags in libc++/libstdc++).
project(vmavificient VERSION 0.1.0 LANGUAGES C CXX)

# Enforce Ninja generator for the top-level build.
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR
        "vmavificient requires the Ninja generator, got '${CMAKE_GENERATOR}'.\n"
        "Reconfigure with:  cmake -G Ninja -B build")
endif()

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    message(FATAL_ERROR "vmavificient requires LLVM/Clang, got ${CMAKE_C_COMPILER_ID}")
endif()
message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} (${CMAKE_C_COMPILER})")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default to Release if nothing was specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Optimization flags (Clang-specific).
#   * -O3 + LTO for release
#   * -march=native so local builds exploit the host CPU (AVX2/NEON)
#   * -fvisibility=hidden to shrink the symbol table
#   * -ffp-contract=fast for cheap FMA in the grain/crop analysis loops
# ---------------------------------------------------------------------------
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "LTO: enabled for Release")
else()
    message(STATUS "LTO: unavailable (${_ipo_err})")
endif()

set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Project-wide optimization flags, applied only to *our* target so that
# third-party deps built in-tree (cjson, etc.) don't choke on -Werror +
# -Wunused-command-line-argument when an option doesn't apply.
set(VMAV_OPT_FLAGS
    -fvisibility=hidden
    -ffp-contract=fast
    -fno-math-errno
    -pipe
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-command-line-argument
)
if(NOT CMAKE_CROSSCOMPILING)
    list(APPEND VMAV_OPT_FLAGS -march=native -mtune=native)
endif()
if(NOT APPLE)
    list(APPEND VMAV_OPT_FLAGS -ffunction-sections -fdata-sections)
endif()

# Linker optimizations: dead-strip + use lld if available.
set(VMAV_LINK_FLAGS "")
# lld is faster than the GNU linker on Linux, but Homebrew's lld crashes with
# LTO on macOS arm64 ("Unsupported stack probing method"), so only use it on
# non-Apple platforms.
if(NOT APPLE)
    find_program(LLVM_LLD ld.lld)
    if(LLVM_LLD)
        list(APPEND VMAV_LINK_FLAGS -fuse-ld=lld)
        message(STATUS "Linker: lld")
    endif()
endif()
if(APPLE)
    list(APPEND VMAV_LINK_FLAGS -Wl,-dead_strip)
else()
    list(APPEND VMAV_LINK_FLAGS -Wl,--gc-sections -Wl,-O2)
endif()

include(ExternalProject)
include(FetchContent)
include(ProcessorCount)

ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 4)
endif()

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------
find_program(CARGO cargo REQUIRED)
find_program(CARGO_CINSTALL cargo-cinstall)
if(NOT CARGO_CINSTALL)
    message(FATAL_ERROR
        "cargo-c is required but not found.\n"
        "Install it with:  cargo install cargo-c")
endif()
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Dependency install prefixes
# ---------------------------------------------------------------------------
set(DEPS_PREFIX     "${CMAKE_BINARY_DIR}/deps")
set(FF_INSTALL      "${DEPS_PREFIX}/ffmpeg")
set(OPUS_INSTALL    "${DEPS_PREFIX}/opus")
set(DOVI_INSTALL    "${DEPS_PREFIX}/libdovi")
set(HDR10_INSTALL   "${DEPS_PREFIX}/libhdr10plus")
set(SVTAV1_INSTALL  "${DEPS_PREFIX}/svtav1")
set(ZLIB_INSTALL    "${DEPS_PREFIX}/zlib")
set(OPENSSL_INSTALL "${DEPS_PREFIX}/openssl")
set(CURL_INSTALL    "${DEPS_PREFIX}/curl")
set(PNG_INSTALL     "${DEPS_PREFIX}/libpng")
set(JPEG_INSTALL    "${DEPS_PREFIX}/libjpeg")
set(TIFF_INSTALL    "${DEPS_PREFIX}/libtiff")
set(LEPT_INSTALL    "${DEPS_PREFIX}/leptonica")
set(TESS_INSTALL    "${DEPS_PREFIX}/tesseract")
set(GRAV1SYNTH_INSTALL "${DEPS_PREFIX}/grav1synth")

# CMake validates INTERFACE_INCLUDE_DIRECTORIES at configure time,
# so the directories must already exist even though headers arrive later.
file(MAKE_DIRECTORY "${FF_INSTALL}/include")
file(MAKE_DIRECTORY "${OPUS_INSTALL}/include")
file(MAKE_DIRECTORY "${DOVI_INSTALL}/include")
file(MAKE_DIRECTORY "${HDR10_INSTALL}/include")
file(MAKE_DIRECTORY "${SVTAV1_INSTALL}/include")
file(MAKE_DIRECTORY "${SVTAV1_INSTALL}/include/svt-av1")
file(MAKE_DIRECTORY "${ZLIB_INSTALL}/include")
file(MAKE_DIRECTORY "${OPENSSL_INSTALL}/include")
file(MAKE_DIRECTORY "${CURL_INSTALL}/include")
file(MAKE_DIRECTORY "${PNG_INSTALL}/include")
file(MAKE_DIRECTORY "${JPEG_INSTALL}/include")
file(MAKE_DIRECTORY "${TIFF_INSTALL}/include")
file(MAKE_DIRECTORY "${LEPT_INSTALL}/include")
file(MAKE_DIRECTORY "${LEPT_INSTALL}/include/leptonica")
file(MAKE_DIRECTORY "${TESS_INSTALL}/include")
file(MAKE_DIRECTORY "${TESS_INSTALL}/include/tesseract")

# ---------------------------------------------------------------------------
# Version pins  (update these to pull newer releases)
# ---------------------------------------------------------------------------
set(FFMPEG_GIT_TAG          "n8.1")
set(OPUS_GIT_TAG            "v1.5.2")
set(LIBDOVI_GIT_TAG         "libdovi-3.3.2")
set(LIBDOVI_VERSION         "3.3.2")
set(LIBHDR10PLUS_GIT_TAG    "libhdr10plus-2.1.5")
set(LIBHDR10PLUS_VERSION    "2.1.5")
set(SVTAV1_GIT_TAG          "v4.0.1")
set(CJSON_GIT_TAG           "v1.7.18")
set(ZLIB_GIT_TAG            "v1.3.1")
set(OPENSSL_GIT_TAG         "openssl-3.3.2")
set(CURL_GIT_TAG            "curl-8_11_0")
set(LIBPNG_GIT_TAG          "v1.6.43")
set(LIBJPEG_GIT_TAG         "3.1.0")
set(LIBTIFF_GIT_TAG         "v4.7.0")
set(LEPTONICA_GIT_TAG       "1.85.0")
set(TESSERACT_GIT_TAG       "5.5.0")
set(GRAV1SYNTH_GIT_REV      "7443e16dad97cc1f47fd7abcd0554e92978b935d")

# ===========================================================================
# cJSON  (lightweight JSON parser – fetched via FetchContent)
# ===========================================================================
FetchContent_Declare(cjson
    GIT_REPOSITORY  https://github.com/DaveGamble/cJSON.git
    GIT_TAG         ${CJSON_GIT_TAG}
    GIT_SHALLOW     TRUE
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# cJSON 1.7.18 still declares cmake_minimum_required(VERSION 2.x), which modern
# CMake refuses. Bump the effective minimum for the nested project only.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(cjson)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

# ===========================================================================
# zlib  (required by ffmpeg, curl, leptonica, and project source)
# ===========================================================================
ExternalProject_Add(ep_zlib
    GIT_REPOSITORY  https://github.com/madler/zlib.git
    GIT_TAG         ${ZLIB_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/zlib"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${ZLIB_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DZLIB_BUILD_EXAMPLES=OFF
        -DBUILD_SHARED_LIBS=OFF
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    # zlib 1.3.1 unconditionally declares the shared `zlib` target via
    # add_library(... SHARED ...), so BUILD_SHARED_LIBS=OFF doesn't suppress it
    # and `cmake --install` always installs both libz.a and libz.so/.dylib.
    # Delete the shared library after install so downstream deps link static.
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "${ZLIB_INSTALL}/lib/libz.so"
        "${ZLIB_INSTALL}/lib/libz.so.1"
        "${ZLIB_INSTALL}/lib/libz.so.1.3.1"
        "${ZLIB_INSTALL}/lib/libz.dylib"
        "${ZLIB_INSTALL}/lib/libz.1.dylib"
        "${ZLIB_INSTALL}/lib/libz.1.3.1.dylib"
    BUILD_BYPRODUCTS "${ZLIB_INSTALL}/lib/libz.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# OpenSSL  (TLS backend for libcurl; Configure script, not CMake)
# ===========================================================================
ExternalProject_Add(ep_openssl
    GIT_REPOSITORY  https://github.com/openssl/openssl.git
    GIT_TAG         ${OPENSSL_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/openssl"
    DEPENDS         ep_zlib
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env
            "CC=${CMAKE_C_COMPILER}"
        <SOURCE_DIR>/config
            --prefix=${OPENSSL_INSTALL}
            --openssldir=${OPENSSL_INSTALL}/ssl
            --libdir=lib
            no-shared
            no-tests
            no-docs
            no-apps
            no-legacy
            zlib
            --with-zlib-include=${ZLIB_INSTALL}/include
            --with-zlib-lib=${ZLIB_INSTALL}/lib
    BUILD_COMMAND   make -j${NPROC}
    INSTALL_COMMAND make install_sw
    BUILD_IN_SOURCE TRUE
    BUILD_BYPRODUCTS
        "${OPENSSL_INSTALL}/lib/libssl.a"
        "${OPENSSL_INSTALL}/lib/libcrypto.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# libcurl  (statically linked HTTP client – used by TMDB lookup)
# ===========================================================================
ExternalProject_Add(ep_curl
    GIT_REPOSITORY  https://github.com/curl/curl.git
    GIT_TAG         ${CURL_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/curl"
    DEPENDS         ep_openssl ep_zlib
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CURL_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_PREFIX_PATH=${OPENSSL_INSTALL}\;${ZLIB_INSTALL}
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_CURL_EXE=OFF
        -DBUILD_TESTING=OFF
        -DCURL_USE_OPENSSL=ON
        -DCURL_USE_LIBSSH2=OFF
        -DCURL_USE_LIBPSL=OFF
        -DCURL_USE_LIBSSH=OFF
        -DCURL_USE_GSSAPI=OFF
        -DUSE_LIBIDN2=OFF
        -DCURL_BROTLI=OFF
        -DCURL_ZSTD=OFF
        -DUSE_NGHTTP2=OFF
        -DUSE_NGTCP2=OFF
        -DUSE_QUICHE=OFF
        -DUSE_LIBRTMP=OFF
        -DCURL_ZLIB=ON
        -DHTTP_ONLY=ON
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
        -DCURL_DISABLE_DICT=ON
        -DCURL_DISABLE_FILE=ON
        -DCURL_DISABLE_FTP=ON
        -DCURL_DISABLE_GOPHER=ON
        -DCURL_DISABLE_IMAP=ON
        -DCURL_DISABLE_MQTT=ON
        -DCURL_DISABLE_POP3=ON
        -DCURL_DISABLE_RTSP=ON
        -DCURL_DISABLE_SMB=ON
        -DCURL_DISABLE_SMTP=ON
        -DCURL_DISABLE_TELNET=ON
        -DCURL_DISABLE_TFTP=ON
        -DOPENSSL_ROOT_DIR=${OPENSSL_INSTALL}
        -DOPENSSL_USE_STATIC_LIBS=ON
        -DZLIB_ROOT=${ZLIB_INSTALL}
        -DZLIB_USE_STATIC_LIBS=ON
    BUILD_COMMAND   cmake --build <BINARY_DIR> --parallel ${NPROC}
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    BUILD_BYPRODUCTS "${CURL_INSTALL}/lib/libcurl.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# libpng  (PNG codec – required by leptonica for subtitle image handling)
# ===========================================================================
ExternalProject_Add(ep_libpng
    GIT_REPOSITORY  https://github.com/glennrp/libpng.git
    GIT_TAG         ${LIBPNG_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/libpng"
    DEPENDS         ep_zlib
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${PNG_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=OFF
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        -DPNG_TESTS=OFF
        -DPNG_TOOLS=OFF
        -DZLIB_ROOT=${ZLIB_INSTALL}
        -DZLIB_USE_STATIC_LIBS=ON
    BUILD_COMMAND   cmake --build <BINARY_DIR> --parallel ${NPROC}
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    BUILD_BYPRODUCTS "${PNG_INSTALL}/lib/libpng16.a" "${PNG_INSTALL}/lib/libpng.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# libjpeg-turbo  (JPEG codec – required by leptonica's TIFF support)
# ===========================================================================
ExternalProject_Add(ep_libjpeg
    GIT_REPOSITORY  https://github.com/libjpeg-turbo/libjpeg-turbo.git
    GIT_TAG         ${LIBJPEG_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/libjpeg"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${JPEG_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_TURBOJPEG=OFF
    BUILD_COMMAND   cmake --build <BINARY_DIR> --parallel ${NPROC}
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    BUILD_BYPRODUCTS "${JPEG_INSTALL}/lib/libjpeg.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# libtiff  (TIFF codec – required by leptonica for bitmap font rendering)
# ===========================================================================
ExternalProject_Add(ep_libtiff
    GIT_REPOSITORY  https://gitlab.com/libtiff/libtiff.git
    GIT_TAG         ${LIBTIFF_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/libtiff"
    DEPENDS         ep_zlib ep_libjpeg
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${TIFF_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_PREFIX_PATH=${JPEG_INSTALL}\;${ZLIB_INSTALL}
        -DBUILD_SHARED_LIBS=OFF
        -Dtiff-tools=OFF
        -Dtiff-tests=OFF
        -Dtiff-contrib=OFF
        -Dtiff-docs=OFF
        -Dtiff-deprecated=OFF
        -Dtiff-install=ON
        -Djpeg=ON
        -DJPEG_INCLUDE_DIR=${JPEG_INSTALL}/include
        -DJPEG_LIBRARY=${JPEG_INSTALL}/lib/libjpeg.a
        -Djbig=OFF
        -Dlerc=OFF
        -Dlzma=OFF
        -Dwebp=OFF
        -Dzstd=OFF
        -Dlibdeflate=OFF
        -Dzlib=ON
        -DZLIB_ROOT=${ZLIB_INSTALL}
        -DZLIB_USE_STATIC_LIBS=ON
    BUILD_COMMAND   cmake --build <BINARY_DIR> --parallel ${NPROC}
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    BUILD_BYPRODUCTS "${TIFF_INSTALL}/lib/libtiff.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# Leptonica  (image library – tesseract's dependency)
# ===========================================================================
ExternalProject_Add(ep_leptonica
    GIT_REPOSITORY  https://github.com/DanBloomberg/leptonica.git
    GIT_TAG         ${LEPTONICA_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/leptonica"
    DEPENDS         ep_zlib ep_libpng ep_libjpeg ep_libtiff
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${LEPT_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_PREFIX_PATH=${ZLIB_INSTALL}\;${PNG_INSTALL}\;${JPEG_INSTALL}\;${TIFF_INSTALL}
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_PROG=OFF
        -DSW_BUILD=OFF
        -DENABLE_ZLIB=ON
        -DENABLE_PNG=ON
        -DENABLE_JPEG=ON
        -DENABLE_TIFF=ON
        -DENABLE_WEBP=OFF
        -DENABLE_OPENJPEG=OFF
        -DENABLE_GIF=OFF
        -DJPEG_INCLUDE_DIR=${JPEG_INSTALL}/include
        -DJPEG_LIBRARY=${JPEG_INSTALL}/lib/libjpeg.a
        -DCMAKE_DISABLE_FIND_PACKAGE_WebP=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_WebPMux=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_GIF=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenJPEG=ON
        -DPNG_ROOT=${PNG_INSTALL}
        -DPNG_PNG_INCLUDE_DIR=${PNG_INSTALL}/include
        -DPNG_LIBRARY=${PNG_INSTALL}/lib/libpng16.a
        -DTIFF_ROOT=${TIFF_INSTALL}
        -DTIFF_INCLUDE_DIR=${TIFF_INSTALL}/include
        -DTIFF_LIBRARY=${TIFF_INSTALL}/lib/libtiff.a
        -DZLIB_ROOT=${ZLIB_INSTALL}
        -DZLIB_USE_STATIC_LIBS=ON
    BUILD_COMMAND   cmake --build <BINARY_DIR> --parallel ${NPROC}
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    BUILD_BYPRODUCTS "${LEPT_INSTALL}/lib/libleptonica.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# Tesseract  (OCR engine used by the subtitle converter)
# ===========================================================================
ExternalProject_Add(ep_tesseract
    GIT_REPOSITORY  https://github.com/tesseract-ocr/tesseract.git
    GIT_TAG         ${TESSERACT_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/tesseract"
    DEPENDS         ep_leptonica
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${TESS_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_PREFIX_PATH=${LEPT_INSTALL}\;${ZLIB_INSTALL}
        -DLeptonica_DIR=${LEPT_INSTALL}/lib/cmake/leptonica
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TRAINING_TOOLS=OFF
        -DBUILD_TESTS=OFF
        -DDISABLED_LEGACY_ENGINE=ON
        -DDISABLE_CURL=ON
        -DDISABLE_ARCHIVE=ON
        -DGRAPHICS_DISABLED=ON
        -DUSE_SYSTEM_ICU=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_LibArchive=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_CURL=ON
    BUILD_COMMAND   cmake --build <BINARY_DIR> --parallel ${NPROC}
    INSTALL_COMMAND cmake --install <BINARY_DIR>
    BUILD_BYPRODUCTS "${TESS_INSTALL}/lib/libtesseract.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# libopus  (reference OPUS codec – built via CMake)
# ===========================================================================
ExternalProject_Add(ep_opus
    GIT_REPOSITORY  https://github.com/xiph/opus.git
    GIT_TAG         ${OPUS_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/opus"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${OPUS_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=OFF
        -DOPUS_BUILD_TESTING=OFF
        -DOPUS_BUILD_PROGRAMS=OFF
    BUILD_COMMAND   cmake --build . --parallel ${NPROC}
    BUILD_BYPRODUCTS "${OPUS_INSTALL}/lib/libopus.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# FFmpeg  (configure / make – depends on libopus)
# ===========================================================================
ExternalProject_Add(ep_ffmpeg
    GIT_REPOSITORY  https://github.com/FFmpeg/FFmpeg.git
    GIT_TAG         ${FFMPEG_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/ffmpeg"
    DEPENDS         ep_opus ep_zlib
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env
            "PKG_CONFIG_PATH=${OPUS_INSTALL}/lib/pkgconfig:${ZLIB_INSTALL}/lib/pkgconfig:${ZLIB_INSTALL}/share/pkgconfig"
        <SOURCE_DIR>/configure
            --prefix=${FF_INSTALL}
            --enable-static
            --disable-shared
            --disable-programs
            --disable-doc
            --disable-autodetect
            --enable-gpl
            --enable-filter=cropdetect
            --enable-libopus
            --enable-encoder=libopus
            --enable-decoder=libopus
            --enable-zlib
            --extra-cflags=-I${OPUS_INSTALL}/include
            --extra-cflags=-I${ZLIB_INSTALL}/include
            --extra-ldflags=-L${OPUS_INSTALL}/lib
            --extra-ldflags=-L${ZLIB_INSTALL}/lib
    BUILD_COMMAND   make -j${NPROC}
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE TRUE
    BUILD_BYPRODUCTS
        "${FF_INSTALL}/lib/libavutil.a"
        "${FF_INSTALL}/lib/libavcodec.a"
        "${FF_INSTALL}/lib/libavformat.a"
        "${FF_INSTALL}/lib/libavfilter.a"
        "${FF_INSTALL}/lib/libswscale.a"
        "${FF_INSTALL}/lib/libswresample.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# libdovi  (Dolby Vision RPU parser – Rust / cargo-c)
# ===========================================================================
ExternalProject_Add(ep_libdovi
    GIT_REPOSITORY  https://github.com/quietvoid/dovi_tool.git
    GIT_TAG         ${LIBDOVI_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/libdovi"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    INSTALL_COMMAND
        ${CARGO} cinstall
            --manifest-path=<SOURCE_DIR>/dolby_vision/Cargo.toml
            --release
            --prefix=${DOVI_INSTALL}
            --libdir=${DOVI_INSTALL}/lib
            --includedir=${DOVI_INSTALL}/include
    BUILD_IN_SOURCE TRUE
    BUILD_BYPRODUCTS "${DOVI_INSTALL}/lib/libdovi.a"
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# libhdr10plus  (HDR10+ metadata parser – Rust / cargo-c)
# ===========================================================================
ExternalProject_Add(ep_libhdr10plus
    GIT_REPOSITORY  https://github.com/quietvoid/hdr10plus_tool.git
    GIT_TAG         ${LIBHDR10PLUS_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/libhdr10plus"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    INSTALL_COMMAND
        ${CARGO} cinstall
            --manifest-path=<SOURCE_DIR>/hdr10plus/Cargo.toml
            --release
            --prefix=${HDR10_INSTALL}
            --libdir=${HDR10_INSTALL}/lib
            --includedir=${HDR10_INSTALL}/include
    BUILD_IN_SOURCE TRUE
    BUILD_BYPRODUCTS "${HDR10_INSTALL}/lib/libhdr10plus-rs.a"
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# SVT-AV1-HDR  (AV1 encoder with Dolby Vision / HDR10+ passthrough)
# ===========================================================================
ExternalProject_Add(ep_svtav1
    GIT_REPOSITORY  https://github.com/juliobbv-p/svt-av1-hdr.git
    GIT_TAG         ${SVTAV1_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/svtav1"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${SVTAV1_INSTALL}
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_APPS=OFF
    BUILD_COMMAND   cmake --build <BINARY_DIR> --parallel ${NPROC}
    BUILD_BYPRODUCTS "${SVTAV1_INSTALL}/lib/libSvtAv1Enc.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# grav1synth  (AV1 film grain analyzer – Rust, installed as a bundled binary)
# The binary is invoked as a subprocess from our C code for accurate per-source
# grain measurement (see src/media_analysis.c). Only the binary is shipped;
# there is no library interface.
# ===========================================================================
set(GRAV1SYNTH_BIN "${GRAV1SYNTH_INSTALL}/bin/grav1synth")
ExternalProject_Add(ep_grav1synth
    DOWNLOAD_COMMAND  ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    PREFIX            "${DEPS_PREFIX}/build/grav1synth"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E env
            "PKG_CONFIG_PATH=${FF_INSTALL}/lib/pkgconfig"
        ${CARGO} install
            --git https://github.com/rust-av/grav1synth.git
            --rev ${GRAV1SYNTH_GIT_REV}
            --locked
            --root ${GRAV1SYNTH_INSTALL}
            grav1synth
    BUILD_BYPRODUCTS "${GRAV1SYNTH_BIN}"
    LOG_INSTALL     TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ===========================================================================
# IMPORTED targets – FFmpeg libraries
# ===========================================================================
set(_ff_libs avutil avcodec avformat avfilter swscale swresample)
foreach(_lib IN LISTS _ff_libs)
    add_library(FFmpeg::${_lib} STATIC IMPORTED GLOBAL)
    set_target_properties(FFmpeg::${_lib} PROPERTIES
        IMPORTED_LOCATION             "${FF_INSTALL}/lib/lib${_lib}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${FF_INSTALL}/include"
    )
    add_dependencies(FFmpeg::${_lib} ep_ffmpeg)
endforeach()

# IMPORTED target – libdovi
add_library(Dovi::dovi STATIC IMPORTED GLOBAL)
set_target_properties(Dovi::dovi PROPERTIES
    IMPORTED_LOCATION             "${DOVI_INSTALL}/lib/libdovi.a"
    INTERFACE_INCLUDE_DIRECTORIES "${DOVI_INSTALL}/include"
)
add_dependencies(Dovi::dovi ep_libdovi)

# IMPORTED target – libhdr10plus
add_library(Hdr10Plus::hdr10plus STATIC IMPORTED GLOBAL)
set_target_properties(Hdr10Plus::hdr10plus PROPERTIES
    IMPORTED_LOCATION             "${HDR10_INSTALL}/lib/libhdr10plus-rs.a"
    INTERFACE_INCLUDE_DIRECTORIES "${HDR10_INSTALL}/include"
)
add_dependencies(Hdr10Plus::hdr10plus ep_libhdr10plus)

# IMPORTED target – SvtAv1Enc
add_library(SvtAv1::Enc STATIC IMPORTED GLOBAL)
set_target_properties(SvtAv1::Enc PROPERTIES
    IMPORTED_LOCATION             "${SVTAV1_INSTALL}/lib/libSvtAv1Enc.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SVTAV1_INSTALL}/include/svt-av1"
)
add_dependencies(SvtAv1::Enc ep_svtav1)

# IMPORTED target – zlib (in-tree static build)
add_library(Vmav::zlib STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::zlib PROPERTIES
    IMPORTED_LOCATION             "${ZLIB_INSTALL}/lib/libz.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INSTALL}/include"
)
add_dependencies(Vmav::zlib ep_zlib)

# IMPORTED targets – OpenSSL (used only as a transitive dep of libcurl)
add_library(Vmav::ssl STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::ssl PROPERTIES
    IMPORTED_LOCATION             "${OPENSSL_INSTALL}/lib/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL}/include"
)
add_dependencies(Vmav::ssl ep_openssl)

add_library(Vmav::crypto STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::crypto PROPERTIES
    IMPORTED_LOCATION             "${OPENSSL_INSTALL}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL}/include"
)
add_dependencies(Vmav::crypto ep_openssl)

# IMPORTED target – libcurl (static)
add_library(Vmav::curl STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::curl PROPERTIES
    IMPORTED_LOCATION             "${CURL_INSTALL}/lib/libcurl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CURL_INSTALL}/include"
    INTERFACE_COMPILE_DEFINITIONS "CURL_STATICLIB"
)
add_dependencies(Vmav::curl ep_curl)

# IMPORTED target – libjpeg-turbo
add_library(Vmav::jpeg STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::jpeg PROPERTIES
    IMPORTED_LOCATION             "${JPEG_INSTALL}/lib/libjpeg.a"
    INTERFACE_INCLUDE_DIRECTORIES "${JPEG_INSTALL}/include"
)
add_dependencies(Vmav::jpeg ep_libjpeg)

# IMPORTED target – libtiff
add_library(Vmav::tiff STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::tiff PROPERTIES
    IMPORTED_LOCATION             "${TIFF_INSTALL}/lib/libtiff.a"
    INTERFACE_INCLUDE_DIRECTORIES "${TIFF_INSTALL}/include"
)
add_dependencies(Vmav::tiff ep_libtiff)

# IMPORTED target – libpng
add_library(Vmav::png STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::png PROPERTIES
    IMPORTED_LOCATION             "${PNG_INSTALL}/lib/libpng16.a"
    INTERFACE_INCLUDE_DIRECTORIES "${PNG_INSTALL}/include"
)
add_dependencies(Vmav::png ep_libpng)

# IMPORTED target – Leptonica
add_library(Vmav::leptonica STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::leptonica PROPERTIES
    IMPORTED_LOCATION             "${LEPT_INSTALL}/lib/libleptonica.a"
    INTERFACE_INCLUDE_DIRECTORIES "${LEPT_INSTALL}/include;${LEPT_INSTALL}/include/leptonica"
)
add_dependencies(Vmav::leptonica ep_leptonica)

# IMPORTED target – Tesseract
add_library(Vmav::tesseract STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::tesseract PROPERTIES
    IMPORTED_LOCATION             "${TESS_INSTALL}/lib/libtesseract.a"
    INTERFACE_INCLUDE_DIRECTORIES "${TESS_INSTALL}/include;${TESS_INSTALL}/include/tesseract"
)
add_dependencies(Vmav::tesseract ep_tesseract)

# ===========================================================================
# Executable
# ===========================================================================
add_executable(vmavificient
    src/main.c
    src/media_info.c
    src/media_tracks.c
    src/media_hdr.c
    src/media_crop.c
    src/media_analysis.c
    src/media_naming.c
    src/audio_encode.c
    src/config.c
    src/encode_preset.c
    src/film_grain.c
    src/rpu_extract.c
    src/video_encode.c
    src/subtitle_convert.c
    src/final_mux.c
    src/tmdb.c
    src/utils.c
)

target_compile_options(vmavificient PRIVATE ${VMAV_OPT_FLAGS})
target_link_options(vmavificient PRIVATE ${VMAV_LINK_FLAGS})

# grav1synth is invoked as a subprocess from src/media_analysis.c;
# expose its absolute install path via a build-time define.
target_compile_definitions(vmavificient PRIVATE
    "VMAV_GRAV1SYNTH_BIN=\"${GRAV1SYNTH_BIN}\"")
add_dependencies(vmavificient ep_grav1synth)

# tesseract is C++; use the C++ linker driver so libc++/libstdc++ is pulled in.
set_target_properties(vmavificient PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(vmavificient PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${cjson_SOURCE_DIR}"
    "${SVTAV1_INSTALL}/include"
)

# Link order matters for static archives: dependents before dependencies.
target_link_libraries(vmavificient PRIVATE
    FFmpeg::avformat
    FFmpeg::avfilter
    FFmpeg::avcodec
    FFmpeg::swresample
    FFmpeg::swscale
    FFmpeg::avutil
    SvtAv1::Enc
    Dovi::dovi
    Hdr10Plus::hdr10plus
    "${OPUS_INSTALL}/lib/libopus.a"
    Vmav::tesseract
    Vmav::leptonica
    Vmav::tiff
    Vmav::jpeg
    Vmav::png
    Vmav::curl
    Vmav::ssl
    Vmav::crypto
    Vmav::zlib
    cjson
    Threads::Threads
    m
)

# macOS needs iconv (for FFmpeg) and frameworks (for Rust std + SecureTransport
# fallbacks referenced by OpenSSL on Apple platforms).
if(APPLE)
    target_link_libraries(vmavificient PRIVATE
        iconv
        "-framework CoreFoundation"
        "-framework Security"
        "-framework SystemConfiguration"
    )
else()
    # OpenSSL's engine layer references dlopen on Linux.
    target_link_libraries(vmavificient PRIVATE dl)
endif()

# ===========================================================================
# Doxygen documentation (optional)
# ===========================================================================
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME        "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_NUMBER      "${PROJECT_VERSION}")
    set(DOXYGEN_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/docs")
    set(DOXYGEN_EXTRACT_ALL          YES)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    doxygen_add_docs(docs
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src"
        COMMENT "Generating API documentation with Doxygen"
    )
endif()
