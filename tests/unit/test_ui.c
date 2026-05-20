#include "vmavificient/vmav_ui.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *g_fp;
static char g_path[256];

void setUp(void) {
    snprintf(g_path, sizeof(g_path), "/tmp/vmav-ui-test-%lld-%d.txt", (long long)getpid(), rand());
    g_fp = fopen(g_path, "w+");
    TEST_ASSERT_NOT_NULL(g_fp);
}

void tearDown(void) {
    if (g_fp != NULL) {
        fclose(g_fp);
        g_fp = NULL;
    }
    (void)remove(g_path);
}

static size_t read_all(char *out, size_t out_size) {
    fflush(g_fp);
    rewind(g_fp);
    size_t n = fread(out, 1, out_size - 1, g_fp);
    out[n] = '\0';
    return n;
}

/* === Progress (non-TTY: appends lines, with percent) ========== */

static void test_progress_non_tty_appends_line_per_update(void) {
    vmav_ui_progress_t *p = vmav_ui_progress_new(g_fp, "Encoding", 100);
    TEST_ASSERT_NOT_NULL(p);
    vmav_ui_progress_set(p, 50);
    vmav_ui_progress_finish(p, "ok");
    vmav_ui_progress_free(p);

    char buf[4096];
    read_all(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Encoding"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "50%"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "100%"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "ok"));
}

static void test_progress_indeterminate_total_zero(void) {
    vmav_ui_progress_t *p = vmav_ui_progress_new(g_fp, "Scanning", 0);
    vmav_ui_progress_set(p, 42);
    vmav_ui_progress_finish(p, NULL);
    vmav_ui_progress_free(p);

    char buf[4096];
    read_all(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Scanning"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "42"));
}

/* === Spinner (non-TTY: silent until finish) =================== */

static void test_spinner_non_tty_only_prints_at_finish(void) {
    vmav_ui_spinner_t *s = vmav_ui_spinner_new(g_fp, "Probing");
    vmav_ui_spinner_tick(s);
    vmav_ui_spinner_tick(s);
    vmav_ui_spinner_finish(s, "OK");
    vmav_ui_spinner_free(s);

    char buf[4096];
    read_all(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Probing"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "OK"));
    /* Frames should NOT appear in non-TTY mode. */
    TEST_ASSERT_NULL(strstr(buf, "/"));
    TEST_ASSERT_NULL(strstr(buf, "\\"));
}

/* === Table ==================================================== */

static void test_table_renders_aligned_kv(void) {
    vmav_ui_table_t *t = vmav_ui_table_new("Plan");
    vmav_status_t st = vmav_ui_table_add(t, "Input", "movie.mkv");
    TEST_ASSERT_TRUE(vmav_status_ok(st));
    st = vmav_ui_table_add(t, "Preset", "live-action");
    TEST_ASSERT_TRUE(vmav_status_ok(st));
    st = vmav_ui_table_add(t, "Target VMAF", "95.0");
    TEST_ASSERT_TRUE(vmav_status_ok(st));
    vmav_ui_table_render(t, g_fp);
    vmav_ui_table_free(t);

    char buf[4096];
    read_all(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Plan"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Input"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "movie.mkv"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Preset"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "live-action"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Target VMAF"));
}

static void test_table_grows_beyond_initial_cap(void) {
    vmav_ui_table_t *t = vmav_ui_table_new(NULL);
    for (int i = 0; i < 30; i++) {
        char k[32];
        snprintf(k, sizeof(k), "key%d", i);
        TEST_ASSERT_TRUE(vmav_status_ok(vmav_ui_table_add(t, k, "v")));
    }
    vmav_ui_table_render(t, g_fp);
    vmav_ui_table_free(t);

    char buf[4096];
    read_all(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "key0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "key29"));
}

/* === Null safety ============================================== */

static void test_null_safe(void) {
    vmav_ui_progress_set(NULL, 10);
    vmav_ui_progress_finish(NULL, "x");
    vmav_ui_progress_free(NULL);
    vmav_ui_spinner_tick(NULL);
    vmav_ui_spinner_finish(NULL, "x");
    vmav_ui_spinner_free(NULL);
    vmav_ui_table_render(NULL, g_fp);
    vmav_ui_table_free(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_progress_non_tty_appends_line_per_update);
    RUN_TEST(test_progress_indeterminate_total_zero);
    RUN_TEST(test_spinner_non_tty_only_prints_at_finish);
    RUN_TEST(test_table_renders_aligned_kv);
    RUN_TEST(test_table_grows_beyond_initial_cap);
    RUN_TEST(test_null_safe);
    return UNITY_END();
}
