#include "vmavificient/vmav_log.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

static char g_tmp_path[256];
static FILE *g_tmp_fp;

void setUp(void) {
    snprintf(g_tmp_path,
             sizeof(g_tmp_path),
             "/tmp/vmav-log-test-%lld-%d.log",
             (long long)getpid(),
             rand());
    g_tmp_fp = fopen(g_tmp_path, "w+");
    TEST_ASSERT_NOT_NULL(g_tmp_fp);
    vmav_log_init(VMAV_LL_TRACE, VMAV_LOG_SINK_FILE);
    vmav_log_set_file(g_tmp_fp);
}

void tearDown(void) {
    if (g_tmp_fp != NULL) {
        fclose(g_tmp_fp);
        g_tmp_fp = NULL;
    }
    (void)remove(g_tmp_path);
}

static size_t read_all(const char *path, char *out, size_t out_size) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    const size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    fclose(fp);
    return n;
}

/* === Level filtering ========================================== */

static void test_level_filter_drops_below_threshold(void) {
    vmav_log_set_level(VMAV_LL_WARN);
    VMAV_LOG_TRACE("trace message");
    VMAV_LOG_DEBUG("debug message");
    VMAV_LOG_INFO("info message");
    VMAV_LOG_WARN("warn message");
    VMAV_LOG_ERROR("error message");
    fflush(g_tmp_fp);

    char content[4096];
    read_all(g_tmp_path, content, sizeof(content));
    TEST_ASSERT_NULL(strstr(content, "trace message"));
    TEST_ASSERT_NULL(strstr(content, "debug message"));
    TEST_ASSERT_NULL(strstr(content, "info message"));
    TEST_ASSERT_NOT_NULL(strstr(content, "warn message"));
    TEST_ASSERT_NOT_NULL(strstr(content, "error message"));
}

/* === Format =================================================== */

static void test_file_sink_format_contains_level_and_loc(void) {
    vmav_log_set_level(VMAV_LL_TRACE);
    VMAV_LOG_INFO("hello %d", 42);
    fflush(g_tmp_fp);

    char content[4096];
    read_all(g_tmp_path, content, sizeof(content));
    TEST_ASSERT_NOT_NULL(strstr(content, "[INFO]"));
    TEST_ASSERT_NOT_NULL(strstr(content, "test_log.c"));
    TEST_ASSERT_NOT_NULL(strstr(content, "hello 42"));
}

/* === Lookup helpers =========================================== */

static void test_level_str_round_trip(void) {
    const vmav_log_level_t levels[] = {
        VMAV_LL_TRACE, VMAV_LL_DEBUG, VMAV_LL_INFO, VMAV_LL_WARN, VMAV_LL_ERROR};
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        const char *s = vmav_log_level_str(levels[i]);
        TEST_ASSERT_NOT_NULL(s);
        vmav_log_level_t parsed;
        vmav_status_t st = vmav_log_level_from_str(s, &parsed);
        TEST_ASSERT_TRUE(vmav_status_ok(st));
        TEST_ASSERT_EQUAL_INT(levels[i], parsed);
    }
}

static void test_level_from_str_unknown_errors(void) {
    vmav_log_level_t parsed;
    vmav_status_t st = vmav_log_level_from_str("loud", &parsed);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

/* === SINK_NONE drops everything =============================== */

static void test_sink_none_drops_all(void) {
    vmav_log_init(VMAV_LL_TRACE, VMAV_LOG_SINK_NONE);
    /* If this writes anywhere, the assertion below would fail because
     * the file is empty. */
    VMAV_LOG_ERROR("would be loud if not dropped");
    fflush(g_tmp_fp);

    char content[256];
    const size_t n = read_all(g_tmp_path, content, sizeof(content));
    TEST_ASSERT_EQUAL_size_t(0, n);

    /* Restore file sink so tearDown's path is consistent. */
    vmav_log_init(VMAV_LL_TRACE, VMAV_LOG_SINK_FILE);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_level_filter_drops_below_threshold);
    RUN_TEST(test_file_sink_format_contains_level_and_loc);
    RUN_TEST(test_level_str_round_trip);
    RUN_TEST(test_level_from_str_unknown_errors);
    RUN_TEST(test_sink_none_drops_all);
    return UNITY_END();
}
