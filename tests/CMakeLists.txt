vmav_add_unit_test(NAME test_result
    SOURCES unit/test_result.c
    LINK_LIBS vmav::foundations)

vmav_add_unit_test(NAME test_os
    SOURCES unit/test_os.c
    LINK_LIBS vmav::foundations)

vmav_add_unit_test(NAME test_log
    SOURCES unit/test_log.c
    LINK_LIBS vmav::foundations)

vmav_add_unit_test(NAME test_subproc
    SOURCES unit/test_subproc.c
    LINK_LIBS vmav::foundations)

vmav_add_unit_test(NAME test_ui
    SOURCES unit/test_ui.c
    LINK_LIBS vmav::foundations)

vmav_add_unit_test(NAME test_cli
    SOURCES unit/test_cli.c
    LINK_LIBS vmav::foundations)

# test_subproc invokes POSIX binaries (/bin/echo, /bin/sleep, /bin/sh)
# that don't exist on Windows. The vmav_subproc Win32 implementation is
# still exercised by the build (it compiles + links), but ctest skips
# this case on Windows. Replace with Windows-native test cases in
# Phase 3 once we have real ffmpeg/grav1synth to invoke.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_tests_properties(test_subproc PROPERTIES DISABLED TRUE)
endif()
