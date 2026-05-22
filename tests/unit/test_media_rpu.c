/* Pure-function tests for vmav_rpu_build_filename + bad-arg paths.
 * Real DV extraction is exercised by integration tests once we have
 * a tagged HEVC fixture in tree. */

#include "vmavificient/vmav_rpu.h"

#include "unity.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_build_filename_appends_suffix(void) {
    char buf[128];
    vmav_rpu_build_filename(buf, sizeof(buf), "/tmp/Movie.2010.1080p");
    TEST_ASSERT_EQUAL_STRING("/tmp/Movie.2010.1080p.rpu.bin", buf);
}

static void test_build_filename_handles_null_buf(void) {
    /* Should not crash with NULL output buffer. */
    vmav_rpu_build_filename(NULL, 128, "ignored");
    vmav_rpu_build_filename((char *)1, 0, "ignored");
    TEST_PASS();
}

static void test_extract_rejects_null_args(void) {
    vmav_rpu_extract_t out;
    vmav_status_t st = vmav_rpu_extract(NULL, "/tmp/o.rpu", &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_rpu_extract("/in.mkv", NULL, &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    st = vmav_rpu_extract("/in.mkv", "/tmp/o.rpu", NULL);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

static void test_extract_missing_input(void) {
    /* Nonexistent input should error out cleanly (no crash, status set). */
    vmav_rpu_extract_t out;
    vmav_status_t st = vmav_rpu_extract("/dev/null/nope.mkv", "/tmp/vmav_rpu_test.bin", &out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(0, out.rpu_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_build_filename_appends_suffix);
    RUN_TEST(test_build_filename_handles_null_buf);
    RUN_TEST(test_extract_rejects_null_args);
    RUN_TEST(test_extract_missing_input);
    return UNITY_END();
}
