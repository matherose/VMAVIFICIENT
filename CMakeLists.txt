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

project(vmavificient VERSION 0.1.0 LANGUAGES C)

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
find_program(LLVM_LLD ld.lld)
if(LLVM_LLD)
    list(APPEND VMAV_LINK_FLAGS -fuse-ld=lld)
    message(STATUS "Linker: lld")
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
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)

# Try system pkg-config first, then Homebrew paths
if(APPLE)
    set(ENV{PKG_CONFIG_PATH}
        "$ENV{PKG_CONFIG_PATH}:/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig")
endif()
pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)

# ---------------------------------------------------------------------------
# Dependency install prefixes
# ---------------------------------------------------------------------------
set(DEPS_PREFIX    "${CMAKE_BINARY_DIR}/deps")
set(FF_INSTALL     "${DEPS_PREFIX}/ffmpeg")
set(OPUS_INSTALL   "${DEPS_PREFIX}/opus")
set(DOVI_INSTALL   "${DEPS_PREFIX}/libdovi")
set(HDR10_INSTALL  "${DEPS_PREFIX}/libhdr10plus")
set(SVTAV1_INSTALL "${DEPS_PREFIX}/svtav1")
set(SSIMU2_INSTALL "${DEPS_PREFIX}/vmav_ssimu2")

# CMake validates INTERFACE_INCLUDE_DIRECTORIES at configure time,
# so the directories must already exist even though headers arrive later.
file(MAKE_DIRECTORY "${FF_INSTALL}/include")
file(MAKE_DIRECTORY "${OPUS_INSTALL}/include")
file(MAKE_DIRECTORY "${DOVI_INSTALL}/include")
file(MAKE_DIRECTORY "${HDR10_INSTALL}/include")
file(MAKE_DIRECTORY "${SVTAV1_INSTALL}/include")
file(MAKE_DIRECTORY "${SVTAV1_INSTALL}/include/svt-av1")
file(MAKE_DIRECTORY "${SSIMU2_INSTALL}/include")

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
FetchContent_MakeAvailable(cjson)

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
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=OFF
        -DOPUS_BUILD_TESTING=OFF
        -DOPUS_BUILD_PROGRAMS=OFF
    BUILD_COMMAND   cmake --build . --parallel ${NPROC}
    BUILD_BYPRODUCTS "${OPUS_INSTALL}/lib/libopus.a"
    LOG_CONFIGURE   TRUE
    LOG_BUILD       TRUE
    LOG_INSTALL     TRUE
)

# ===========================================================================
# FFmpeg  (configure / make – depends on libopus)
# ===========================================================================
ExternalProject_Add(ep_ffmpeg
    GIT_REPOSITORY  https://github.com/FFmpeg/FFmpeg.git
    GIT_TAG         ${FFMPEG_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/ffmpeg"
    DEPENDS         ep_opus
    CONFIGURE_COMMAND
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
            --extra-cflags=-I${OPUS_INSTALL}/include
            --extra-ldflags=-L${OPUS_INSTALL}/lib
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
)

# ===========================================================================
# vmav_ssimu2  (in-tree Rust staticlib wrapping the ssimulacra2 perceptual
#               metric; built with cargo, linked into the C binary)
# ===========================================================================
set(SSIMU2_CRATE_DIR "${CMAKE_SOURCE_DIR}/rust/vmav_ssimu2")
set(SSIMU2_CARGO_TARGET_DIR "${DEPS_PREFIX}/build/vmav_ssimu2")
set(SSIMU2_STATIC_LIB "${SSIMU2_INSTALL}/lib/libvmav_ssimu2.a")

ExternalProject_Add(ep_vmav_ssimu2
    SOURCE_DIR        "${SSIMU2_CRATE_DIR}"
    PREFIX            "${DEPS_PREFIX}/build/vmav_ssimu2_ep"
    DOWNLOAD_COMMAND  ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${SSIMU2_CARGO_TARGET_DIR}
            ${CARGO} build --release --manifest-path=${SSIMU2_CRATE_DIR}/Cargo.toml
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${SSIMU2_INSTALL}/lib
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
            ${SSIMU2_CARGO_TARGET_DIR}/release/libvmav_ssimu2.a
            ${SSIMU2_STATIC_LIB}
    BUILD_BYPRODUCTS "${SSIMU2_STATIC_LIB}"
    LOG_BUILD     TRUE
    LOG_INSTALL   TRUE
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

# IMPORTED target – vmav_ssimu2 (embedded SSIMULACRA2)
add_library(Vmav::ssimu2 STATIC IMPORTED GLOBAL)
set_target_properties(Vmav::ssimu2 PROPERTIES
    IMPORTED_LOCATION             "${SSIMU2_STATIC_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include"
)
add_dependencies(Vmav::ssimu2 ep_vmav_ssimu2)

# IMPORTED target – SvtAv1Enc
add_library(SvtAv1::Enc STATIC IMPORTED GLOBAL)
set_target_properties(SvtAv1::Enc PROPERTIES
    IMPORTED_LOCATION             "${SVTAV1_INSTALL}/lib/libSvtAv1Enc.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SVTAV1_INSTALL}/include/svt-av1"
)
add_dependencies(SvtAv1::Enc ep_svtav1)

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
    src/media_ssimu2.c
    src/media_naming.c
    src/audio_encode.c
    src/config.c
    src/encode_preset.c
    src/rpu_extract.c
    src/video_encode.c
    src/crf_search.c
    src/subtitle_convert.c
    src/final_mux.c
    src/tmdb.c
    src/utils.c
)

target_compile_options(vmavificient PRIVATE ${VMAV_OPT_FLAGS})
target_link_options(vmavificient PRIVATE ${VMAV_LINK_FLAGS})

target_include_directories(vmavificient PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${cjson_SOURCE_DIR}"
    "${SVTAV1_INSTALL}/include"
    ${TESSERACT_INCLUDE_DIRS}
    ${LEPTONICA_INCLUDE_DIRS}
)

# Link directories for Tesseract/Leptonica (from pkg-config)
target_link_directories(vmavificient PRIVATE
    ${TESSERACT_LIBRARY_DIRS}
    ${LEPTONICA_LIBRARY_DIRS}
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
    Vmav::ssimu2
    Dovi::dovi
    Hdr10Plus::hdr10plus
    "${OPUS_INSTALL}/lib/libopus.a"
    Threads::Threads
    CURL::libcurl
    ${TESSERACT_LIBRARIES}
    ${LEPTONICA_LIBRARIES}
    cjson
    z
    m
)

# macOS needs iconv (for FFmpeg) and frameworks (for Rust std)
if(APPLE)
    target_link_libraries(vmavificient PRIVATE
        iconv
        "-framework CoreFoundation"
        "-framework Security"
    )
endif()

# ===========================================================================
# Integration test: ssimu2_score_files (self-scoring sanity check)
# ===========================================================================
add_executable(test_ssimu2_files
    rust/vmav_ssimu2/test_files.c
    src/media_ssimu2.c
)
target_include_directories(test_ssimu2_files PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${FF_INSTALL}/include"
)
target_link_libraries(test_ssimu2_files PRIVATE
    FFmpeg::avformat
    FFmpeg::avfilter
    FFmpeg::avcodec
    FFmpeg::swresample
    FFmpeg::swscale
    FFmpeg::avutil
    Vmav::ssimu2
    "${OPUS_INSTALL}/lib/libopus.a"
    Threads::Threads
    z
    m
)
if(APPLE)
    target_link_libraries(test_ssimu2_files PRIVATE
        iconv
        "-framework CoreFoundation"
        "-framework Security"
    )
endif()

# ===========================================================================
# Integration test: crf_search_run (end-to-end probe on a real source)
# ===========================================================================
add_executable(test_crf_search
    rust/vmav_ssimu2/test_crf_search.c
    src/crf_search.c
    src/video_encode.c
    src/media_ssimu2.c
    src/media_info.c
    src/media_hdr.c
    src/media_crop.c
    src/encode_preset.c
)
target_include_directories(test_crf_search PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${FF_INSTALL}/include"
    "${SVTAV1_INSTALL}/include"
)
target_link_libraries(test_crf_search PRIVATE
    FFmpeg::avformat
    FFmpeg::avfilter
    FFmpeg::avcodec
    FFmpeg::swresample
    FFmpeg::swscale
    FFmpeg::avutil
    SvtAv1::Enc
    Vmav::ssimu2
    Dovi::dovi
    Hdr10Plus::hdr10plus
    "${OPUS_INSTALL}/lib/libopus.a"
    Threads::Threads
    z
    m
)
if(APPLE)
    target_link_libraries(test_crf_search PRIVATE
        iconv
        "-framework CoreFoundation"
        "-framework Security"
    )
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
