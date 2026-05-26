cmake_minimum_required(VERSION 3.24)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" VMAV_VERSION_RAW)
string(STRIP "${VMAV_VERSION_RAW}" VMAV_VERSION_FULL)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" VMAV_VERSION "${VMAV_VERSION_FULL}")
if(NOT VMAV_VERSION)
    message(FATAL_ERROR "Could not parse VERSION file: '${VMAV_VERSION_FULL}'")
endif()

project(vmavificient
    VERSION ${VMAV_VERSION}
    DESCRIPTION "Professional-grade AV1 encoding CLI"
    HOMEPAGE_URL "https://github.com/joeltordjman/VMAVIFICIENT"
    # CXX needed because Tesseract (vmav::tp::tesseract) is a C++17
    # static lib. The project source stays in C; we just need CMake
    # to know about the C++ toolchain so it can pick the C++ driver
    # when linking binaries that consume Tesseract (via the
    # IMPORTED_LINK_INTERFACE_LANGUAGES=CXX property on
    # vmav_tp_tesseract).
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

option(VMAV_BUILD_TESTS    "Build unit + integration tests"             ON)
option(VMAV_SANITIZE       "Build with ASan + UBSan"                    OFF)
option(VMAV_LTO            "Enable link-time optimization"              ON)
option(VMAV_USE_THIRD_PARTY "Build vendored third_party deps"           ON)
option(VMAV_REPRODUCIBLE   "Enable reproducible-build flags"            ON)
option(VMAV_FETCH_REAL_CONTENT "Fetch BBB/ToS real-content fixtures"    OFF)
option(VMAV_BUNDLE_TESSDATA "Fetch + bundle eng.traineddata for packaging" OFF)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang"
   AND NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    message(FATAL_ERROR
        "vmavificient requires Clang/LLVM. Detected: ${CMAKE_C_COMPILER_ID}. "
        "GCC and MSVC are not supported.")
endif()

include(VmavCompileOptions)
include(VmavReproducible)

set(VMAV_GIT_SHA "unknown")
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_git_sha)
        set(VMAV_GIT_SHA "${_git_sha}")
    endif()
endif()

string(TIMESTAMP VMAV_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(
    "${CMAKE_SOURCE_DIR}/include/vmavificient/vmav_version.h.in"
    "${CMAKE_BINARY_DIR}/include/vmavificient/vmav_version.h"
    @ONLY)

if(VMAV_USE_THIRD_PARTY)
    include(VmavThirdParty)
endif()

add_subdirectory(src)

if(VMAV_BUILD_TESTS)
    enable_testing()
    include(VmavTesting)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS vmavificient RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE LICENSE-BINARY README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# Phase 9: bundle eng.traineddata so PGS-subtitle OCR works on a clean
# install without a system tesseract-ocr-eng package. Default OFF —
# the dev workflow uses system tessdata. Packaging builds set this ON.
# Fetched by scripts/fetch_tessdata.sh into a gitignored cache dir,
# sha256-pinned to tesseract-ocr/tessdata@4.1.0. The custom_target
# guarantees the file exists before the install rule fires.
set(_vmav_tessdata_cache_dir "${CMAKE_SOURCE_DIR}/tests/fixtures/.tessdata-cache")
set(_vmav_tessdata_file "${_vmav_tessdata_cache_dir}/eng.traineddata")
if(VMAV_BUNDLE_TESSDATA)
    add_custom_command(
        OUTPUT "${_vmav_tessdata_file}"
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch_tessdata.sh"
                --cache-dir "${_vmav_tessdata_cache_dir}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Fetching pinned eng.traineddata for packaging"
        VERBATIM)
    add_custom_target(vmav_tessdata ALL DEPENDS "${_vmav_tessdata_file}")
    # Install into ${prefix}/share/vmavificient/tessdata/eng.traineddata.
    # GNUInstallDirs gives us DATADIR == share on POSIX. The resolver in
    # subtitle_convert.c knows to look here.
    install(FILES "${_vmav_tessdata_file}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/vmavificient/tessdata")
endif()

message(STATUS "")
message(STATUS "vmavificient ${VMAV_VERSION_FULL} (${VMAV_GIT_SHA})")
message(STATUS "  C compiler:   ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:        ${VMAV_BUILD_TESTS}")
message(STATUS "  LTO:          ${VMAV_LTO}")
message(STATUS "  Sanitizers:   ${VMAV_SANITIZE}")
message(STATUS "  Third-party:  ${VMAV_USE_THIRD_PARTY}")
message(STATUS "  Reproducible: ${VMAV_REPRODUCIBLE}")
message(STATUS "")
