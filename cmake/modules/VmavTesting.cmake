# VmavTesting.cmake — Unity-based unit test registration helper.
#
# Usage:
#   vmav_add_unit_test(NAME test_result SOURCES test_result.c
#                      LINK_LIBS vmav::foundations)
# Registers a CTest entry. Unity is vendored under third_party/unity/ in
# later phases; for Phase 0 this module is a stub that just registers
# CTest entries built from plain C, no framework yet.

include_guard(GLOBAL)

function(vmav_add_unit_test)
    cmake_parse_arguments(VMAV_TEST "" "NAME" "SOURCES;LINK_LIBS" ${ARGN})
    if(NOT VMAV_TEST_NAME OR NOT VMAV_TEST_SOURCES)
        message(FATAL_ERROR "vmav_add_unit_test: NAME and SOURCES required")
    endif()
    add_executable(${VMAV_TEST_NAME} ${VMAV_TEST_SOURCES})
    target_link_libraries(${VMAV_TEST_NAME}
        PRIVATE
            vmav::compile_options
            ${VMAV_TEST_LINK_LIBS})
    target_include_directories(${VMAV_TEST_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include)
    add_test(NAME ${VMAV_TEST_NAME} COMMAND ${VMAV_TEST_NAME})
endfunction()
