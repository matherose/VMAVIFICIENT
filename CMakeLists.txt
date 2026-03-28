cmake_minimum_required(VERSION 3.24)
project(vmavificient VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

# ---------------------------------------------------------------------------
# Dependency install prefixes
# ---------------------------------------------------------------------------
set(DEPS_PREFIX    "${CMAKE_BINARY_DIR}/deps")
set(FF_INSTALL     "${DEPS_PREFIX}/ffmpeg")
set(DOVI_INSTALL   "${DEPS_PREFIX}/libdovi")
set(HDR10_INSTALL  "${DEPS_PREFIX}/libhdr10plus")

# CMake validates INTERFACE_INCLUDE_DIRECTORIES at configure time,
# so the directories must already exist even though headers arrive later.
file(MAKE_DIRECTORY "${FF_INSTALL}/include")
file(MAKE_DIRECTORY "${DOVI_INSTALL}/include")
file(MAKE_DIRECTORY "${HDR10_INSTALL}/include")

# ---------------------------------------------------------------------------
# Version pins  (update these to pull newer releases)
# ---------------------------------------------------------------------------
set(FFMPEG_GIT_TAG          "n7.1")
set(LIBDOVI_GIT_TAG         "libdovi-3.3.2")
set(LIBDOVI_VERSION         "3.3.2")
set(LIBHDR10PLUS_GIT_TAG    "libhdr10plus-2.1.5")
set(LIBHDR10PLUS_VERSION    "2.1.5")
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
# FFmpeg  (configure / make)
# ===========================================================================
ExternalProject_Add(ep_ffmpeg
    GIT_REPOSITORY  https://github.com/FFmpeg/FFmpeg.git
    GIT_TAG         ${FFMPEG_GIT_TAG}
    GIT_SHALLOW     TRUE
    PREFIX          "${DEPS_PREFIX}/build/ffmpeg"
    CONFIGURE_COMMAND
        <SOURCE_DIR>/configure
            --prefix=${FF_INSTALL}
            --enable-static
            --disable-shared
            --disable-programs
            --disable-doc
            --disable-autodetect
            --enable-gpl
            --disable-postproc
            --enable-filter=cropdetect
    BUILD_COMMAND   make -j${NPROC}
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE TRUE
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
    src/tmdb.c
    src/utils.c
)

target_include_directories(vmavificient PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${cjson_SOURCE_DIR}"
)

# Link order matters for static archives: dependents before dependencies.
target_link_libraries(vmavificient PRIVATE
    FFmpeg::avformat
    FFmpeg::avfilter
    FFmpeg::avcodec
    FFmpeg::swresample
    FFmpeg::swscale
    FFmpeg::avutil
    Dovi::dovi
    Hdr10Plus::hdr10plus
    Threads::Threads
    CURL::libcurl
    cjson
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
