# tests/fixtures/CMakeLists.txt — build-time generation of integration
# test inputs. Nothing in this directory is shipped; the outputs live
# under ${CMAKE_BINARY_DIR}/tests/fixtures/generated/.

add_executable(vmav_fixturegen fixturegen.c)
target_link_libraries(vmav_fixturegen PRIVATE vmav::compile_options)

set(_fixture_dir "${CMAKE_BINARY_DIR}/tests/fixtures/generated")
set(_tiny_y4m "${_fixture_dir}/tiny.y4m")

# Cross-build path: when targeting Windows from Linux, the fixture
# generator is itself a .exe and needs the same emulator (wine) that
# ctest uses for the test runners. Native builds leave the variable
# empty and the COMMAND list collapses to just the binary.
if(CMAKE_CROSSCOMPILING_EMULATOR)
    set(_fg_invoke ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:vmav_fixturegen>)
else()
    set(_fg_invoke $<TARGET_FILE:vmav_fixturegen>)
endif()

add_custom_command(
    OUTPUT "${_tiny_y4m}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_fixture_dir}"
    COMMAND ${_fg_invoke} "${_tiny_y4m}"
    DEPENDS vmav_fixturegen
    COMMENT "Generating tests/fixtures/tiny.y4m"
    VERBATIM)

add_custom_target(vmav_fixtures DEPENDS "${_tiny_y4m}")

# Export the on-disk fixture location to consuming tests via a target
# property so vmav_add_integration_test() can stamp it in as a compile
# definition without re-deriving the path.
set_target_properties(vmav_fixtures PROPERTIES
    VMAV_FIXTURE_DIR "${_fixture_dir}")
