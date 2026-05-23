/* Bad-arg tests for vmav_video_encode. Real encode round-trip requires
 * a YUV fixture — that goes into integration tests once we have one
 * in tree. */

#include "vmavificient/vmav_video_encode.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_rejects_null_args(void) {
    vmav_video_encode_spec_t s = {0};
    vmav_video_encode_result_t out;
    vmav_status_t st = vmav_video_encode(NULL, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_video_encode(&s, NULL);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

static void test_rejects_missing_paths(void) {
    vmav_video_encode_spec_t s = {.preset = VMAV_PRESET_LIVE_ACTION, .crf = 32};
    vmav_video_encode_result_t out;
    /* No input_path / output_path. */
    vmav_status_t st = vmav_video_encode(&s, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

static void test_rejects_missing_input_file(void) {
    vmav_video_encode_spec_t s = {
        .input_path = "/dev/null/nonexistent.mkv",
        .output_path = "/tmp/vmav_test_unused.ivf",
        .preset = VMAV_PRESET_LIVE_ACTION,
        .crf = 32,
    };
    vmav_video_encode_result_t out;
    const vmav_status_t st = vmav_video_encode(&s, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_null_args);
    RUN_TEST(test_rejects_missing_paths);
    RUN_TEST(test_rejects_missing_input_file);
    return UNITY_END();
}
