/* Pure-function tests for vmav_subtitle_*.  Real PGS→SRT conversion
 * needs a tessdata fixture + sample PGS bitstream; that goes into
 * integration tests once we have an in-tree fixture. */

#include "vmavificient/vmav_subtitle.h"

#include "unity.h"

#include <string.h>

#include <libavcodec/avcodec.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ---- iso639 → tesseract mapping ---- */

static void test_iso639_known_codes(void) {
    TEST_ASSERT_EQUAL_STRING("eng", vmav_subtitle_iso639_to_tesseract("eng"));
    TEST_ASSERT_EQUAL_STRING("fra", vmav_subtitle_iso639_to_tesseract("fre"));
    TEST_ASSERT_EQUAL_STRING("fra", vmav_subtitle_iso639_to_tesseract("fra"));
    TEST_ASSERT_EQUAL_STRING("deu", vmav_subtitle_iso639_to_tesseract("ger"));
    TEST_ASSERT_EQUAL_STRING("chi_sim", vmav_subtitle_iso639_to_tesseract("chi"));
    TEST_ASSERT_EQUAL_STRING("chi_sim", vmav_subtitle_iso639_to_tesseract("zho"));
}

static void test_iso639_defaults_eng(void) {
    TEST_ASSERT_EQUAL_STRING("eng", vmav_subtitle_iso639_to_tesseract(NULL));
    TEST_ASSERT_EQUAL_STRING("eng", vmav_subtitle_iso639_to_tesseract(""));
}

static void test_iso639_passes_through_unknown(void) {
    /* Custom traineddata names should round-trip unchanged. */
    TEST_ASSERT_EQUAL_STRING("Custom+eng", vmav_subtitle_iso639_to_tesseract("Custom+eng"));
}

/* ---- SRT filename builder ---- */

static void test_srt_filename_english_full(void) {
    char buf[128];
    vmav_subtitle_build_srt_filename(
        buf, sizeof(buf), "/tmp/Movie", "eng", VMAV_FR_UNKNOWN, false, false);
    TEST_ASSERT_EQUAL_STRING("/tmp/Movie.eng.full.srt", buf);
}

static void test_srt_filename_english_forced(void) {
    char buf[128];
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", "eng", VMAV_FR_UNKNOWN, true, false);
    TEST_ASSERT_EQUAL_STRING("/x/M.eng.forced.srt", buf);
}

static void test_srt_filename_english_sdh(void) {
    char buf[128];
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", "eng", VMAV_FR_UNKNOWN, false, true);
    TEST_ASSERT_EQUAL_STRING("/x/M.eng.sdh.srt", buf);
}

/* Forced wins over SDH if both flags set (rare but possible). */
static void test_srt_filename_forced_beats_sdh(void) {
    char buf[128];
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", "eng", VMAV_FR_UNKNOWN, true, true);
    TEST_ASSERT_EQUAL_STRING("/x/M.eng.forced.srt", buf);
}

static void test_srt_filename_french_variants(void) {
    char buf[128];
    /* VFF/UNKNOWN → fre.fr */
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", "fre", VMAV_FR_VFF, false, false);
    TEST_ASSERT_EQUAL_STRING("/x/M.fre.fr.full.srt", buf);
    vmav_subtitle_build_srt_filename(
        buf, sizeof(buf), "/x/M", "fra", VMAV_FR_UNKNOWN, false, false);
    TEST_ASSERT_EQUAL_STRING("/x/M.fre.fr.full.srt", buf);
    /* VFQ → fre.ca */
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", "fre", VMAV_FR_VFQ, false, true);
    TEST_ASSERT_EQUAL_STRING("/x/M.fre.ca.sdh.srt", buf);
    /* VFI → fre.vfi */
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", "fre", VMAV_FR_VFI, true, false);
    TEST_ASSERT_EQUAL_STRING("/x/M.fre.vfi.forced.srt", buf);
}

static void test_srt_filename_unknown_lang(void) {
    char buf[128];
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", "", VMAV_FR_UNKNOWN, false, false);
    TEST_ASSERT_EQUAL_STRING("/x/M.und.full.srt", buf);
    vmav_subtitle_build_srt_filename(buf, sizeof(buf), "/x/M", NULL, VMAV_FR_UNKNOWN, false, false);
    TEST_ASSERT_EQUAL_STRING("/x/M.und.full.srt", buf);
}

static void test_srt_filename_handles_null_buf(void) {
    vmav_subtitle_build_srt_filename(NULL, 128, "ignored", "eng", VMAV_FR_UNKNOWN, false, false);
    vmav_subtitle_build_srt_filename((char *)1, 0, "ignored", "eng", VMAV_FR_UNKNOWN, false, false);
    TEST_PASS();
}

/* ---- Codec ID classification ---- */

static void test_is_pgs(void) {
    vmav_track_t t = {.codec_id = AV_CODEC_ID_HDMV_PGS_SUBTITLE};
    TEST_ASSERT_TRUE(vmav_subtitle_is_pgs(&t));

    t.codec_id = AV_CODEC_ID_SUBRIP;
    TEST_ASSERT_FALSE(vmav_subtitle_is_pgs(&t));

    TEST_ASSERT_FALSE(vmav_subtitle_is_pgs(NULL));
}

static void test_is_text(void) {
    vmav_track_t t = {.codec_id = AV_CODEC_ID_SUBRIP};
    TEST_ASSERT_TRUE(vmav_subtitle_is_text(&t));
    t.codec_id = AV_CODEC_ID_ASS;
    TEST_ASSERT_TRUE(vmav_subtitle_is_text(&t));
    t.codec_id = AV_CODEC_ID_WEBVTT;
    TEST_ASSERT_TRUE(vmav_subtitle_is_text(&t));
    t.codec_id = AV_CODEC_ID_MOV_TEXT;
    TEST_ASSERT_TRUE(vmav_subtitle_is_text(&t));

    /* PGS is NOT text. */
    t.codec_id = AV_CODEC_ID_HDMV_PGS_SUBTITLE;
    TEST_ASSERT_FALSE(vmav_subtitle_is_text(&t));

    TEST_ASSERT_FALSE(vmav_subtitle_is_text(NULL));
}

/* ---- Bad-arg paths ---- */

static void test_convert_rejects_null_args(void) {
    vmav_track_t track = {.codec_id = AV_CODEC_ID_HDMV_PGS_SUBTITLE, .stream_index = 1};
    vmav_subtitle_convert_t out;
    vmav_status_t st = vmav_subtitle_convert_pgs(NULL, &track, "/tmp/o.srt", NULL, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_subtitle_convert_pgs("/in.mkv", NULL, "/tmp/o.srt", NULL, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_subtitle_convert_pgs("/in.mkv", &track, NULL, NULL, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_subtitle_convert_pgs("/in.mkv", &track, "/tmp/o.srt", NULL, NULL);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_iso639_known_codes);
    RUN_TEST(test_iso639_defaults_eng);
    RUN_TEST(test_iso639_passes_through_unknown);
    RUN_TEST(test_srt_filename_english_full);
    RUN_TEST(test_srt_filename_english_forced);
    RUN_TEST(test_srt_filename_english_sdh);
    RUN_TEST(test_srt_filename_forced_beats_sdh);
    RUN_TEST(test_srt_filename_french_variants);
    RUN_TEST(test_srt_filename_unknown_lang);
    RUN_TEST(test_srt_filename_handles_null_buf);
    RUN_TEST(test_is_pgs);
    RUN_TEST(test_is_text);
    RUN_TEST(test_convert_rejects_null_args);
    return UNITY_END();
}
