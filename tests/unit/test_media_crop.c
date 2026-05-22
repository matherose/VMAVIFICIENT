/* Error-path coverage for vmav_crop_probe. Happy-path probing needs
 * tiny video fixtures (Phase 5). */

#include "vmavificient/vmav_crop.h"

#include "unity.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_null_arg(void) {
    vmav_crop_rect_t r;
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, vmav_crop_probe(NULL, &r).code);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, vmav_crop_probe("/dev/null", NULL).code);
}

static void test_nonexistent_file(void) {
    vmav_crop_rect_t r;
    vmav_status_t st = vmav_crop_probe("/nonexistent.mkv", &r);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null_arg);
    RUN_TEST(test_nonexistent_file);
    return UNITY_END();
}
