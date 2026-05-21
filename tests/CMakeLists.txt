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

vmav_add_unit_test(NAME test_str_utils
    SOURCES unit/test_str_utils.c
    LINK_LIBS vmav::util)

vmav_add_unit_test(NAME test_pathbuf
    SOURCES unit/test_pathbuf.c
    LINK_LIBS vmav::util)

vmav_add_unit_test(NAME test_encoder_preset
    SOURCES unit/test_encoder_preset.c
    LINK_LIBS vmav::encode)

vmav_add_unit_test(NAME test_json_io
    SOURCES unit/test_json_io.c
    LINK_LIBS vmav::util vmav::tp::cjson)

vmav_add_unit_test(NAME test_cache
    SOURCES unit/test_cache.c
    LINK_LIBS vmav::foundations vmav::util vmav::tp::cjson)

vmav_add_unit_test(NAME test_zlib_link
    SOURCES unit/test_zlib_link.c
    LINK_LIBS vmav::tp::zlib)

vmav_add_unit_test(NAME test_libpng_link
    SOURCES unit/test_libpng_link.c
    LINK_LIBS vmav::tp::png)

vmav_add_unit_test(NAME test_libjpeg_link
    SOURCES unit/test_libjpeg_link.c
    LINK_LIBS vmav::tp::jpeg)

vmav_add_unit_test(NAME test_libtiff_link
    SOURCES unit/test_libtiff_link.c
    LINK_LIBS vmav::tp::tiff)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # test_subproc: invokes POSIX-only binaries (/bin/echo, /bin/sh).
    # test_cache: relies on env-var cache-dir override and `rm -rf`
    # cleanup; the Win32 cache dir comes from SHGetKnownFolderPath
    # which ignores HOME/XDG_CACHE_HOME. Re-add Windows-native test
    # cases for both in Phase 4.
    set_tests_properties(test_subproc PROPERTIES DISABLED TRUE)
    set_tests_properties(test_cache PROPERTIES DISABLED TRUE)
endif()
