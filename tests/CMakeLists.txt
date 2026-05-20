vmav_add_unit_test(NAME test_result
    SOURCES unit/test_result.c
    LINK_LIBS vmav::foundations)

vmav_add_unit_test(NAME test_os
    SOURCES unit/test_os.c
    LINK_LIBS vmav::foundations)

vmav_add_unit_test(NAME test_log
    SOURCES unit/test_log.c
    LINK_LIBS vmav::foundations)
