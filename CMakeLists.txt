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
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

option(VMAV_BUILD_TESTS    "Build unit + integration tests"             ON)
option(VMAV_SANITIZE       "Build with ASan + UBSan"                    OFF)
option(VMAV_LTO            "Enable link-time optimization"              ON)
option(VMAV_USE_THIRD_PARTY "Build vendored third_party deps"           ON)
option(VMAV_REPRODUCIBLE   "Enable reproducible-build flags"            ON)

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
