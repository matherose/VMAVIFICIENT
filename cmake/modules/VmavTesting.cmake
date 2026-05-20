# VmavTesting.cmake — vendored Unity static library + helper to register
# unit tests with CTest.
#
# Unity is at third_party/unity/ as a git submodule, pinned to v2.6.0.
# Build it once into the `vmav::unity` target and reuse across every
# test executable.

include_guard(GLOBAL)

set(_unity_dir "${CMAKE_SOURCE_DIR}/third_party/unity")
if(NOT EXISTS "${_unity_dir}/src/unity.c")
    message(FATAL_ERROR
        "Unity submodule missing at ${_unity_dir}.\n"
        "Run:  git submodule update --init --recursive")
endif()

add_library(vmav_unity STATIC "${_unity_dir}/src/unity.c")
add_library(vmav::unity ALIAS vmav_unity)

# Treat Unity headers as SYSTEM so our strict -Wpedantic / -Werror
# doesn't flag the macro tricks Unity uses internally.
target_include_directories(vmav_unity SYSTEM PUBLIC "${_unity_dir}/src")

target_compile_definitions(vmav_unity PUBLIC
    UNITY_INCLUDE_DOUBLE
    UNITY_OUTPUT_COLOR)

# Suppress warnings inside Unity itself; we only care about cleanliness
# of our own code.
target_compile_options(vmav_unity PRIVATE -w)

function(vmav_add_unit_test)
    cmake_parse_arguments(VMAV_TEST "" "NAME" "SOURCES;LINK_LIBS" ${ARGN})
    if(NOT VMAV_TEST_NAME OR NOT VMAV_TEST_SOURCES)
        message(FATAL_ERROR "vmav_add_unit_test: NAME and SOURCES required")
    endif()
    add_executable(${VMAV_TEST_NAME} ${VMAV_TEST_SOURCES})
    target_link_libraries(${VMAV_TEST_NAME}
        PRIVATE
            vmav::compile_options
            vmav::unity
            ${VMAV_TEST_LINK_LIBS})
    target_include_directories(${VMAV_TEST_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include)
    add_test(NAME ${VMAV_TEST_NAME} COMMAND ${VMAV_TEST_NAME})
endfunction()
