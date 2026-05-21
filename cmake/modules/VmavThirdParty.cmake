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
