# Criterion.cmake — Criterion test framework integration per AGENTS.md § 17
# CMake helper for Criterion unit tests
# This file is included by tests/unit/CMakeLists.txt

# ── Find Criterion package ────────────────────────────────────────────────────
# Uses pkg-config to find Criterion installation
# ─────────────────────────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)
pkg_check_modules(CRITERION REQUIRED criterion)

# ── Add Criterion test target ────────────────────────────────────────────────
# Usage: add_criterion_test(<target> [SOURCES <files>] [LINK_LIBS <libs>])
# Example:
#   add_criterion_test(test_module_a
#     SOURCES test_module_a.c
#     LINK_LIBS module_a
#   )
# ─────────────────────────────────────────────────────────────────────────────

function(add_criterion_test target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBS" ${ARGN})

    #sources needed
    if(NOT ARG_SOURCES)
        set(ARG_SOURCES "${target}.c")
    endif()

    # Add executable target
    add_executable(${target} ${ARG_SOURCES})

    # Include paths
    target_include_directories(${target} PRIVATE
        ${CRITERION_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/include  # For generated headers
    )

    # Link libraries
    target_link_libraries(${target} PRIVATE
        ${CRITERION_LIBRARIES}
        ${ARG_LINK_LIBS}
    )

    # Criterion flags
    target_compile_options(${target} PRIVATE ${CRITERION_CFLAGS_OTHER})

    # Register with CTest
    add_test(NAME ${target} COMMAND ${target})

    # Set environment for verbose output
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "CRITERION_VERBOSITY=normal"
    )

    message(STATUS "Criterion: Added test ${target}")
endfunction()

# ── Add parameterized test target ────────────────────────────────────────────
# Same as add_criterion_test but for parameterized tests
# ─────────────────────────────────────────────────────────────────────────────

function(add_criterion_parameterized_test target)
    add_criterion_test(${target} ${ARGN})
endfunction()

# ── RunCriterion framework helper ────────────────────────────────────────────
# Creates test suite with optional setup/teardown
# Usage:
#   vmav_add_test_suite(name [SUITE_INIT function] [SUITE_FINI function])
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_add_test_suite name)
    cmake_parse_arguments(ARG "" "SUITE_INIT;SUITE_FINI" "" ${ARGN})

    # Write test suite boilerplate (if needed for auto-generation)
    # For now, this is a placeholder for future test infrastructure
    message(STATUS "Criterion: Test suite ${name} configured")
endfunction()
