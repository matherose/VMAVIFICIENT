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

vmav_add_unit_test(NAME test_openssl_link
    SOURCES unit/test_openssl_link.c
    LINK_LIBS vmav::tp::ssl vmav::tp::crypto)

vmav_add_unit_test(NAME test_libcurl_link
    SOURCES unit/test_libcurl_link.c
    LINK_LIBS vmav::tp::curl)

vmav_add_unit_test(NAME test_tmdb_match
    SOURCES unit/test_tmdb_match.c
    LINK_LIBS vmav::integrations)

vmav_add_unit_test(NAME test_ffmpeg_link
    SOURCES unit/test_ffmpeg_link.c
    LINK_LIBS vmav::tp::avformat vmav::tp::avfilter)

vmav_add_unit_test(NAME test_media_probe
    SOURCES unit/test_media_probe.c
    LINK_LIBS vmav::media)

vmav_add_unit_test(NAME test_media_hdr
    SOURCES unit/test_media_hdr.c
    LINK_LIBS vmav::media)

vmav_add_unit_test(NAME test_media_tracks
    SOURCES unit/test_media_tracks.c
    LINK_LIBS vmav::media)

vmav_add_unit_test(NAME test_media_crop
    SOURCES unit/test_media_crop.c
    LINK_LIBS vmav::media)

vmav_add_unit_test(NAME test_media_analysis
    SOURCES unit/test_media_analysis.c
    LINK_LIBS vmav::media)

vmav_add_unit_test(NAME test_media_naming
    SOURCES unit/test_media_naming.c
    LINK_LIBS vmav::media)

vmav_add_unit_test(NAME test_libdovi_link
    SOURCES unit/test_libdovi_link.c
    LINK_LIBS vmav::tp::dovi)

vmav_add_unit_test(NAME test_libhdr10plus_link
    SOURCES unit/test_libhdr10plus_link.c
    LINK_LIBS vmav::tp::hdr10plus)

vmav_add_unit_test(NAME test_media_rpu
    SOURCES unit/test_media_rpu.c
    LINK_LIBS vmav::media)

vmav_add_unit_test(NAME test_audio_encode
    SOURCES unit/test_audio_encode.c
    LINK_LIBS vmav::encode)

vmav_add_unit_test(NAME test_tesseract_link
    SOURCES unit/test_tesseract_link.c
    LINK_LIBS vmav::tp::tesseract vmav::tp::leptonica)

vmav_add_unit_test(NAME test_subtitle_convert
    SOURCES unit/test_subtitle_convert.c
    LINK_LIBS vmav::encode vmav::tp::avcodec)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # test_subproc: invokes POSIX-only binaries (/bin/echo, /bin/sh).
    # test_cache: relies on env-var cache-dir override and `rm -rf`
    # cleanup; the Win32 cache dir comes from SHGetKnownFolderPath
    # which ignores HOME/XDG_CACHE_HOME. Re-add Windows-native test
    # cases for both in Phase 4.
    set_tests_properties(test_subproc PROPERTIES DISABLED TRUE)
    set_tests_properties(test_cache PROPERTIES DISABLED TRUE)
endif()
