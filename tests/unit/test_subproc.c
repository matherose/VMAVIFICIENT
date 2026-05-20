#include "vmavificient/vmav_subproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* === Basic spawn ============================================== */

static void test_echo_captures_stdout(void) {
    const char *argv[] = {"echo", "hello", "world", NULL};
    vmav_subproc_spec_t spec = {
        .exe = "/bin/echo", .argv = argv, .capture_stdout = true};
    vmav_subproc_result_t r = {0};
    vmav_status_t st = vmav_subproc_run(&spec, &r);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_EQUAL_INT(0, r.exit_code);
    TEST_ASSERT_FALSE(r.signaled);
    TEST_ASSERT_FALSE(r.timed_out);
    TEST_ASSERT_NOT_NULL(r.stdout_buf.data);
    TEST_ASSERT_EQUAL_STRING("hello world\n", r.stdout_buf.data);
    vmav_subproc_result_free(&r);
}

static void test_false_exits_nonzero(void) {
    const char *argv[] = {"false", NULL};
    vmav_subproc_spec_t spec = {.exe = "/usr/bin/false", .argv = argv};
    /* /bin/false on macOS, /usr/bin/false on some Linuxes. Try both. */
    vmav_subproc_result_t r = {0};
    vmav_status_t st = vmav_subproc_run(&spec, &r);
    if (!vmav_status_ok(st)) {
        spec.exe = "/bin/false";
        st = vmav_subproc_run(&spec, &r);
    }
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_EQUAL(0, r.exit_code);
    vmav_subproc_result_free(&r);
}

static void test_stdout_and_stderr_separated(void) {
    const char *argv[] = {"sh", "-c", "printf out; printf err >&2", NULL};
    vmav_subproc_spec_t spec = {
        .exe = "/bin/sh", .argv = argv,
        .capture_stdout = true, .capture_stderr = true};
    vmav_subproc_result_t r = {0};
    vmav_status_t st = vmav_subproc_run(&spec, &r);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_EQUAL_INT(0, r.exit_code);
    TEST_ASSERT_NOT_NULL(r.stdout_buf.data);
    TEST_ASSERT_EQUAL_STRING("out", r.stdout_buf.data);
    TEST_ASSERT_NOT_NULL(r.stderr_buf.data);
    TEST_ASSERT_EQUAL_STRING("err", r.stderr_buf.data);
    vmav_subproc_result_free(&r);
}

/* === Timeout ================================================= */

static void test_timeout_kills_child(void) {
    const char *argv[] = {"sleep", "5", NULL};
    vmav_subproc_spec_t spec = {
        .exe = "/bin/sleep", .argv = argv,
        .timeout_ms = 100};
    vmav_subproc_result_t r = {0};
    vmav_status_t st = vmav_subproc_run(&spec, &r);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_TRUE(r.timed_out);
    /* Either signaled (SIGTERM/SIGKILL = 15/9) or exited <0; either
     * is acceptable. wall_ms must be far less than the 5s sleep. */
    TEST_ASSERT_TRUE(r.wall_ms < 2000);
    vmav_subproc_result_free(&r);
}

/* === Error paths ============================================== */

static void test_nonexistent_exe_errors(void) {
    const char *argv[] = {"this-binary-definitely-does-not-exist", NULL};
    vmav_subproc_spec_t spec = {
        .exe = "this-binary-definitely-does-not-exist", .argv = argv};
    vmav_subproc_result_t r = {0};
    vmav_status_t st = vmav_subproc_run(&spec, &r);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_SUBPROC, st.code);
    vmav_subproc_result_free(&r);
}

static void test_null_args_errors(void) {
    vmav_subproc_result_t r = {0};
    vmav_status_t st = vmav_subproc_run(NULL, &r);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);

    vmav_subproc_spec_t spec = {.exe = NULL, .argv = NULL};
    st = vmav_subproc_run(&spec, &r);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

/* === Result_free is idempotent =============================== */

static void test_result_free_idempotent(void) {
    vmav_subproc_result_t r = {0};
    vmav_subproc_result_free(&r);
    vmav_subproc_result_free(&r);
    vmav_subproc_result_free(NULL);
    TEST_PASS();
}

/* === Captures large output ==================================== */

static void test_captures_large_output(void) {
    /* 64 KB of 'A' via yes(1)-like sh loop. Verifies that the buf grows. */
    const char *argv[] = {"sh", "-c",
                          "for i in $(seq 1 1024); do "
                          "printf '%64s' '' | tr ' ' A; done; echo", NULL};
    vmav_subproc_spec_t spec = {
        .exe = "/bin/sh", .argv = argv, .capture_stdout = true};
    vmav_subproc_result_t r = {0};
    vmav_status_t st = vmav_subproc_run(&spec, &r);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_EQUAL_INT(0, r.exit_code);
    TEST_ASSERT_GREATER_THAN_size_t(60000, r.stdout_buf.len);
    vmav_subproc_result_free(&r);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_echo_captures_stdout);
    RUN_TEST(test_false_exits_nonzero);
    RUN_TEST(test_stdout_and_stderr_separated);
    RUN_TEST(test_timeout_kills_child);
    RUN_TEST(test_nonexistent_exe_errors);
    RUN_TEST(test_null_args_errors);
    RUN_TEST(test_result_free_idempotent);
    RUN_TEST(test_captures_large_output);
    return UNITY_END();
}
