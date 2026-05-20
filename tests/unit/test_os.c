#include "vmavificient/vmav_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* === Path join ================================================ */

static void test_path_join_basic(void) {
    char buf[VMAV_PATH_MAX];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_path_join(buf, sizeof(buf), "/tmp", "foo")));
    TEST_ASSERT_EQUAL_STRING("/tmp/foo", buf);
}

static void test_path_join_strips_trailing_slash(void) {
    char buf[VMAV_PATH_MAX];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_path_join(buf, sizeof(buf), "/tmp/", "foo")));
    TEST_ASSERT_EQUAL_STRING("/tmp/foo", buf);
}

static void test_path_join_strips_leading_slash_on_b(void) {
    char buf[VMAV_PATH_MAX];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_path_join(buf, sizeof(buf), "/tmp", "/foo")));
    /* If b is absolute, b wins; that's deliberate (matches Python os.path.join). */
    TEST_ASSERT_EQUAL_STRING("/foo", buf);
}

static void test_path_join_empty_a(void) {
    char buf[VMAV_PATH_MAX];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_path_join(buf, sizeof(buf), "", "foo")));
    TEST_ASSERT_EQUAL_STRING("foo", buf);
}

static void test_path_join_empty_b(void) {
    char buf[VMAV_PATH_MAX];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_path_join(buf, sizeof(buf), "/tmp", "")));
    TEST_ASSERT_EQUAL_STRING("/tmp", buf);
}

static void test_path_join_truncation_errors(void) {
    char buf[8];
    vmav_status_t st = vmav_path_join(buf, sizeof(buf), "/usr/local", "share");
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

/* === Terminal ================================================ */

static void test_no_color_detects_env(void) {
    /* Save / restore caller env. */
    const char *prev = vmav_env_get("NO_COLOR");
    setenv("NO_COLOR", "1", 1);
    TEST_ASSERT_TRUE(vmav_term_no_color());
    unsetenv("NO_COLOR");
    /* If TERM is dumb, no_color is still true. */
    setenv("TERM", "dumb", 1);
    TEST_ASSERT_TRUE(vmav_term_no_color());
    unsetenv("TERM");
    if (prev != NULL) {
        setenv("NO_COLOR", prev, 1);
    }
}

/* === Filesystem ============================================== */

static void test_mkdir_p_creates_nested_dirs(void) {
    const char *tmp = vmav_env_get("TMPDIR");
    if (tmp == NULL) {
        tmp = "/tmp";
    }
    char dir[VMAV_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/vmav-test-mkdir-%lld/a/b/c", tmp, (long long)getpid());

    vmav_status_t st = vmav_fs_mkdir_p(dir);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_TRUE(vmav_fs_is_dir(dir));

    /* Idempotent: calling again succeeds. */
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_fs_mkdir_p(dir)));

    /* Cleanup. */
    char cleanup[VMAV_PATH_MAX];
    snprintf(cleanup, sizeof(cleanup), "rm -rf %s/vmav-test-mkdir-%lld",
             tmp, (long long)getpid());
    (void)system(cleanup);
}

/* === Time ==================================================== */

static void test_time_now_ms_monotonic(void) {
    const uint64_t a = vmav_time_now_ms();
    /* Some platforms may have 1ms resolution; do a busy loop. */
    for (volatile int i = 0; i < 1000000; ++i) {
    }
    const uint64_t b = vmav_time_now_ms();
    TEST_ASSERT_TRUE(b >= a);
}

static void test_iso8601_format_is_well_formed(void) {
    char buf[32];
    vmav_status_t st = vmav_time_now_iso8601(buf, sizeof(buf));
    TEST_ASSERT_TRUE(vmav_status_ok(st));
    /* Must end with 'Z'. */
    TEST_ASSERT_EQUAL_CHAR('Z', buf[strlen(buf) - 1]);
    /* Length is exactly 20: YYYY-MM-DDTHH:MM:SSZ */
    TEST_ASSERT_EQUAL_size_t(20, strlen(buf));
}

static void test_iso8601_small_buffer_errors(void) {
    char buf[8];
    vmav_status_t st = vmav_time_now_iso8601(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

/* === Config / cache dirs ===================================== */

static void test_config_dir_contains_vmavificient(void) {
    char buf[VMAV_PATH_MAX];
    vmav_status_t st = vmav_path_config_dir(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_NULL(strstr(buf, "vmavificient"));
}

static void test_cache_dir_contains_vmavificient(void) {
    char buf[VMAV_PATH_MAX];
    vmav_status_t st = vmav_path_cache_dir(buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_NULL(strstr(buf, "vmavificient"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_path_join_basic);
    RUN_TEST(test_path_join_strips_trailing_slash);
    RUN_TEST(test_path_join_strips_leading_slash_on_b);
    RUN_TEST(test_path_join_empty_a);
    RUN_TEST(test_path_join_empty_b);
    RUN_TEST(test_path_join_truncation_errors);
    RUN_TEST(test_no_color_detects_env);
    RUN_TEST(test_mkdir_p_creates_nested_dirs);
    RUN_TEST(test_time_now_ms_monotonic);
    RUN_TEST(test_iso8601_format_is_well_formed);
    RUN_TEST(test_iso8601_small_buffer_errors);
    RUN_TEST(test_config_dir_contains_vmavificient);
    RUN_TEST(test_cache_dir_contains_vmavificient);
    return UNITY_END();
}
