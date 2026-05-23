/* Tests for the encoder_vmaf module:
 *   - open + close round-trip (loads the built-in vmaf_v0.6.1 model)
 *   - bad-arg paths
 *   - finalize before any submit errors cleanly
 * Real measurement (submit YUV pairs → get a score) needs reference
 * + distorted frame fixtures; that's m5/m6 integration territory. */

#include "vmavificient/vmav_vmaf.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static vmav_vmaf_spec_t basic_spec(void) {
    return (vmav_vmaf_spec_t){
        .width = 1920,
        .height = 1080,
        .bit_depth = 10,
        .n_threads = 1,
        .model = NULL, /* default = vmaf_v0.6.1 */
    };
}

static void test_open_close_default_model(void) {
    vmav_vmaf_spec_t s = basic_spec();
    vmav_vmaf_t *m = NULL;
    vmav_status_t st = vmav_vmaf_open(&s, &m);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_NULL(m);
    vmav_vmaf_close(m);
}

static void test_open_close_explicit_model(void) {
    vmav_vmaf_spec_t s = basic_spec();
    s.model = "vmaf_v0.6.1";
    vmav_vmaf_t *m = NULL;
    vmav_status_t st = vmav_vmaf_open(&s, &m);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    vmav_vmaf_close(m);
}

static void test_open_rejects_unknown_model(void) {
    vmav_vmaf_spec_t s = basic_spec();
    s.model = "vmaf_v999.totally_not_a_model";
    vmav_vmaf_t *m = NULL;
    vmav_status_t st = vmav_vmaf_open(&s, &m);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_NULL(m);
}

static void test_open_rejects_bad_args(void) {
    vmav_vmaf_spec_t s = basic_spec();
    vmav_vmaf_t *m = NULL;

    vmav_status_t st = vmav_vmaf_open(NULL, &m);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_vmaf_open(&s, NULL);
    TEST_ASSERT_FALSE(vmav_status_ok(st));

    s.width = 0;
    st = vmav_vmaf_open(&s, &m);
    TEST_ASSERT_FALSE(vmav_status_ok(st));

    s = basic_spec();
    s.bit_depth = 12;
    st = vmav_vmaf_open(&s, &m);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

static void test_close_null_is_safe(void) {
    vmav_vmaf_close(NULL);
    TEST_PASS();
}

static void test_finalize_without_submits_errors(void) {
    vmav_vmaf_spec_t s = basic_spec();
    vmav_vmaf_t *m = NULL;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_vmaf_open(&s, &m)));
    double score = -1.0;
    const vmav_status_t st = vmav_vmaf_finalize(m, &score);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    vmav_vmaf_close(m);
}

static void test_finalize_rejects_null_args(void) {
    double score = 0.0;
    vmav_status_t st = vmav_vmaf_finalize(NULL, &score);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_open_close_default_model);
    RUN_TEST(test_open_close_explicit_model);
    RUN_TEST(test_open_rejects_unknown_model);
    RUN_TEST(test_open_rejects_bad_args);
    RUN_TEST(test_close_null_is_safe);
    RUN_TEST(test_finalize_without_submits_errors);
    RUN_TEST(test_finalize_rejects_null_args);
    return UNITY_END();
}
