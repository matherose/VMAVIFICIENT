/* Tests for vmav_hdr_probe. Same constraint as test_media_probe: we
 * don't have real HDR fixtures yet (those need Phase 5 — tiny .mkv
 * fixtures), so these tests focus on error paths. */

#include "vmavificient/vmav_hdr.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_null_arg_errors(void) {
    vmav_hdr_info_t info;
    vmav_status_t st = vmav_hdr_probe(NULL, &info);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_hdr_probe("/dev/null", NULL);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

static void test_nonexistent_file(void) {
    vmav_hdr_info_t info;
    vmav_status_t st = vmav_hdr_probe("/nonexistent/forsure.mkv", &info);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

static void test_defaults_on_failure(void) {
    vmav_hdr_info_t info;
    /* Garbage to start. */
    memset(&info, 0xAB, sizeof(info));
    vmav_status_t st = vmav_hdr_probe("/nonexistent/forsure.mkv", &info);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    /* Probe should have zeroed/initialized the struct before failing
     * the open. dv_profile/dv_level default to -1, the rest false. */
    TEST_ASSERT_FALSE(info.has_dolby_vision);
    TEST_ASSERT_FALSE(info.has_hdr10);
    TEST_ASSERT_FALSE(info.has_hdr10plus);
    TEST_ASSERT_EQUAL_INT(-1, info.dv_profile);
    TEST_ASSERT_EQUAL_INT(-1, info.dv_level);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null_arg_errors);
    RUN_TEST(test_nonexistent_file);
    RUN_TEST(test_defaults_on_failure);
    return UNITY_END();
}
