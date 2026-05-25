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

# ============================================================
# Phase 8: real-content fixtures (BBB + optionally ToS HDR).
#
# Opt-in via -DVMAV_FETCH_REAL_CONTENT=ON. The fetch script
# downloads to a deterministic cache dir + sha256-validates;
# CI keys actions/cache by the script's content + URL list so
# any change invalidates the cache. Local dev builds default
# to OFF so a clean clone without network access still works.
# Integration tests gated on this look for files under
# ${_real_content_dir} and skip cleanly when absent.
# ============================================================
set(_real_content_dir "${CMAKE_SOURCE_DIR}/tests/fixtures/.real-content-cache")
if(VMAV_FETCH_REAL_CONTENT)
    set(_bbb_clip "${_real_content_dir}/bbb_clip.mkv")
    add_custom_command(
        OUTPUT "${_bbb_clip}"
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch_real_content.sh"
                --cache-dir "${_real_content_dir}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Fetching real-content fixtures (BBB) to ${_real_content_dir}"
        VERBATIM)
    add_custom_target(vmav_real_content DEPENDS "${_bbb_clip}")
    set_target_properties(vmav_real_content PROPERTIES
        VMAV_REAL_CONTENT_DIR "${_real_content_dir}")
    # Promote the BBB clip into the standard fixture set so integration
    # tests that consume it can DEPENDS on vmav_fixtures uniformly.
    add_dependencies(vmav_fixtures vmav_real_content)
endif()

set_target_properties(vmav_fixtures PROPERTIES
    VMAV_REAL_CONTENT_DIR "${_real_content_dir}"
    VMAV_REAL_CONTENT_ENABLED "${VMAV_FETCH_REAL_CONTENT}")
