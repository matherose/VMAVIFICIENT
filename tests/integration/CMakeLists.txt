# tests/integration/CMakeLists.txt — slower, end-to-end tests that
# consume the on-disk fixtures generated under tests/fixtures/.
#
# Filter from the command line:
#   ctest -L integration     # only this directory
#   ctest -LE integration    # everything except this directory

vmav_add_integration_test(NAME it_fixtures_smoke
    SOURCES it_fixtures_smoke.c
    LINK_LIBS vmav::media)

vmav_add_integration_test(NAME it_encode_smoke
    SOURCES it_encode_smoke.c
    LINK_LIBS vmav::media vmav::foundations)
