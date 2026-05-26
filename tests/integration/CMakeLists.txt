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

vmav_add_integration_test(NAME it_encode_resume
    SOURCES it_encode_resume.c
    LINK_LIBS vmav::media vmav::foundations vmav::encode)

# Phase 8 m2: real-content smoke on a CC-licensed clip (Big Buck Bunny).
# Compiles unconditionally but auto-skips when VMAV_FETCH_REAL_CONTENT
# is OFF (the CMake option default) — the runtime branch keys on the
# VMAV_REAL_CONTENT_ENABLED compile def.
vmav_add_integration_test(NAME it_encode_bbb
    SOURCES it_encode_bbb.c
    LINK_LIBS vmav::media vmav::foundations vmav::encode)

# Phase 8 m4: HDR10 passthrough regression. Decodes the source +
# output with libav directly to harvest mastering display + CLL side
# data (same code path the encoder uses internally).
vmav_add_integration_test(NAME it_encode_hdr
    SOURCES it_encode_hdr.c
    LINK_LIBS vmav::media vmav::foundations vmav::encode vmav::tp::avformat vmav::tp::avcodec vmav::tp::avutil)

# Phase 5 m3 (it_pgs_to_srt) intentionally deferred to post-v2.0.
# Generating a real PGS bitstream programmatically needs either
# ffmpeg's pgssub *encoder* (only the decoder ships in stock builds)
# or ~300 lines of fixture C to hand-author a multi-segment PGS
# (PCS/WDS/PDS/ODS/END) + bitmap rasterizer for Tesseract to OCR.
# The unit suite (tests/unit/test_subtitle_convert.c) already covers
# everything testable without a real bitmap (iso639 mapping, filename
# builder, codec classification, bad-arg paths); the PGS parser itself
# is bit-for-bit ported from v1 which has years of Blu-ray runtime
# validation. Worth revisiting if we ever hit a real regression here.
