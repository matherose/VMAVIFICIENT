/* Tests for vmav_media_probe. We don't bundle real video fixtures yet
 * (those land in Phase 5), so these tests focus on error paths: null
 * args, missing files, non-video files. Happy-path probing is covered
 * by integration testing once we have tiny .mkv fixtures. */

#include "vmavificient/vmav_media.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_empty_path[256];

void setUp(void) {
    snprintf(g_empty_path, sizeof(g_empty_path), "/tmp/vmav-probe-test-%d.bin", (int)getpid());
    FILE *fp = fopen(g_empty_path, "wb");
    if (fp != NULL) {
        fclose(fp);
    }
}

void tearDown(void) {
    (void)remove(g_empty_path);
}

static void test_null_arg_errors(void) {
    vmav_media_info_t info;
    vmav_status_t st = vmav_media_probe(NULL, &info);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_media_probe("/dev/null", NULL);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

static void test_nonexistent_file(void) {
    vmav_media_info_t info;
    vmav_status_t st = vmav_media_probe("/nonexistent/path/forsure.mkv", &info);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    /* The exact code depends on FFmpeg's error: ENOENT maps to IO. */
    TEST_ASSERT_TRUE(st.code == VMAV_ERR_IO || st.code == VMAV_ERR_FFMPEG);
}

static void test_empty_file_is_not_video(void) {
    vmav_media_info_t info;
    vmav_status_t st = vmav_media_probe(g_empty_path, &info);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    /* Empty file can't be opened as any container. */
    TEST_ASSERT_TRUE(st.code == VMAV_ERR_IO || st.code == VMAV_ERR_PARSE ||
                     st.code == VMAV_ERR_FFMPEG || st.code == VMAV_ERR_NOT_FOUND);
}

static void test_zero_init_on_failure(void) {
    vmav_media_info_t info;
    /* Pre-populate with garbage to make sure probe zeroes on failure. */
    memset(&info, 0xAB, sizeof(info));
    vmav_status_t st = vmav_media_probe("/nonexistent/path/forsure.mkv", &info);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    /* Most fields should now be zero (memset(0) at entry). */
    TEST_ASSERT_EQUAL_INT(0, info.width);
    TEST_ASSERT_EQUAL_INT(0, info.height);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null_arg_errors);
    RUN_TEST(test_nonexistent_file);
    RUN_TEST(test_empty_file_is_not_video);
    RUN_TEST(test_zero_init_on_failure);
    return UNITY_END();
}
