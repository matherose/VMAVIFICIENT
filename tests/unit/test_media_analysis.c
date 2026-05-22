/* Error-path coverage for vmav_grain_analyze. Happy path needs tiny
 * video fixtures (Phase 5). */

#include "vmavificient/vmav_analysis.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_null_arg(void) {
    vmav_grain_score_t s;
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, vmav_grain_analyze(NULL, &s).code);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, vmav_grain_analyze("/dev/null", NULL).code);
}

static void test_nonexistent_file(void) {
    vmav_grain_score_t s;
    vmav_status_t st = vmav_grain_analyze("/nonexistent.mkv", &s);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null_arg);
    RUN_TEST(test_nonexistent_file);
    return UNITY_END();
}
