/* Pure-function tests for vmav_audio_build_filename + bad-arg paths.
 * Actual encode round-trip is exercised by integration tests once we
 * have an audio fixture in tree. */

#include "vmavificient/vmav_audio.h"

#include "unity.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_filename_english_track(void) {
    char buf[128];
    vmav_audio_build_filename(buf, sizeof(buf), "/tmp/Inception.2010", "eng", VMAV_FR_UNKNOWN);
    TEST_ASSERT_EQUAL_STRING("/tmp/Inception.2010.eng.opus", buf);
}

static void test_filename_french_vff_default(void) {
    char buf[128];
    vmav_audio_build_filename(buf, sizeof(buf), "/x/Movie", "fre", VMAV_FR_VFF);
    TEST_ASSERT_EQUAL_STRING("/x/Movie.fre.fr.opus", buf);
    /* VFF and UNKNOWN both default to fre.fr. */
    vmav_audio_build_filename(buf, sizeof(buf), "/x/Movie", "fre", VMAV_FR_UNKNOWN);
    TEST_ASSERT_EQUAL_STRING("/x/Movie.fre.fr.opus", buf);
}

static void test_filename_french_vfq(void) {
    char buf[128];
    vmav_audio_build_filename(buf, sizeof(buf), "/x/Movie", "fra", VMAV_FR_VFQ);
    TEST_ASSERT_EQUAL_STRING("/x/Movie.fre.ca.opus", buf);
}

static void test_filename_french_vfi(void) {
    char buf[128];
    vmav_audio_build_filename(buf, sizeof(buf), "/x/Movie", "fre", VMAV_FR_VFI);
    TEST_ASSERT_EQUAL_STRING("/x/Movie.fre.vfi.opus", buf);
}

static void test_filename_unknown_lang_defaults_to_und(void) {
    char buf[128];
    vmav_audio_build_filename(buf, sizeof(buf), "/x/Movie", "", VMAV_FR_UNKNOWN);
    TEST_ASSERT_EQUAL_STRING("/x/Movie.und.opus", buf);
    vmav_audio_build_filename(buf, sizeof(buf), "/x/Movie", NULL, VMAV_FR_UNKNOWN);
    TEST_ASSERT_EQUAL_STRING("/x/Movie.und.opus", buf);
}

static void test_filename_handles_null_buf(void) {
    vmav_audio_build_filename(NULL, 128, "ignored", "eng", VMAV_FR_UNKNOWN);
    vmav_audio_build_filename((char *)1, 0, "ignored", "eng", VMAV_FR_UNKNOWN);
    TEST_PASS();
}

static void test_encode_rejects_null_args(void) {
    vmav_track_t track = {.stream_index = 1, .channels = 2};
    vmav_audio_encode_t out;
    vmav_status_t st = vmav_audio_encode_track(NULL, &track, "/tmp/o.opus", &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_audio_encode_track("/in.mkv", NULL, "/tmp/o.opus", &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_audio_encode_track("/in.mkv", &track, NULL, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_audio_encode_track("/in.mkv", &track, "/tmp/o.opus", NULL);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

static void test_encode_missing_input(void) {
    vmav_track_t track = {.stream_index = 0, .channels = 2};
    vmav_audio_encode_t out;
    vmav_status_t st =
        vmav_audio_encode_track("/dev/null/nope.mkv", &track, "/tmp/vmav_audio_test.opus", &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_filename_english_track);
    RUN_TEST(test_filename_french_vff_default);
    RUN_TEST(test_filename_french_vfq);
    RUN_TEST(test_filename_french_vfi);
    RUN_TEST(test_filename_unknown_lang_defaults_to_und);
    RUN_TEST(test_filename_handles_null_buf);
    RUN_TEST(test_encode_rejects_null_args);
    RUN_TEST(test_encode_missing_input);
    return UNITY_END();
}
