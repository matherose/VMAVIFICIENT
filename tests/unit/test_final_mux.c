/* Bad-arg tests for vmav_final_mux. The real subprocess path (invokes
 * ffmpeg, reads/writes files) needs a video fixture; we only verify
 * the argument-validation layer here. */

#include "vmavificient/vmav_final_mux.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_rejects_null_args(void) {
    vmav_final_mux_spec_t s = {0};
    vmav_final_mux_result_t out;
    vmav_status_t st = vmav_final_mux(NULL, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_final_mux(&s, NULL);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

static void test_rejects_missing_video_path(void) {
    vmav_final_mux_spec_t s = {.output_path = "/tmp/vmav_test_unused.mkv"};
    vmav_final_mux_result_t out;
    const vmav_status_t st = vmav_final_mux(&s, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_null_args);
    RUN_TEST(test_rejects_missing_video_path);
    return UNITY_END();
}
