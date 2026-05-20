#include "vmavificient/vmav_result.h"

#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ok_status_is_zero(void) {
    vmav_status_t st = VMAV_OK_STATUS;
    TEST_ASSERT_EQUAL_INT(VMAV_OK, st.code);
    TEST_ASSERT_TRUE(vmav_status_ok(st));
    TEST_ASSERT_NULL(st.file);
    TEST_ASSERT_EQUAL_INT(0, st.line);
    TEST_ASSERT_EQUAL_STRING("", st.msg);
}

static void test_err_macro_captures_location(void) {
    vmav_status_t st = VMAV_ERR(VMAV_ERR_IO, "fake io error: %d", 42);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_IO, st.code);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_NOT_NULL(st.file);
    TEST_ASSERT_NOT_NULL(strstr(st.file, "test_result.c"));
    TEST_ASSERT_GREATER_THAN_INT(0, st.line);
    TEST_ASSERT_EQUAL_STRING("fake io error: 42", st.msg);
}

static void test_code_str_covers_every_code(void) {
    /* If a new code is added without updating vmav_code_str, the
     * default branch returns "unknown" — assert all named codes
     * resolve to a non-"unknown" string. */
    const vmav_code_t codes[] = {
        VMAV_OK,           VMAV_ERR_GENERIC,    VMAV_ERR_IO,         VMAV_ERR_PARSE,
        VMAV_ERR_NO_MEM,   VMAV_ERR_BAD_ARG,    VMAV_ERR_NOT_FOUND,  VMAV_ERR_NOT_IMPL,
        VMAV_ERR_PERMISSION, VMAV_ERR_TIMEOUT,  VMAV_ERR_CANCELED,   VMAV_ERR_SUBPROC,
        VMAV_ERR_FFMPEG,   VMAV_ERR_ENCODE,     VMAV_ERR_DECODE,     VMAV_ERR_INVARIANT};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        const char *s = vmav_code_str(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_NOT_EQUAL(0, strcmp(s, "unknown"));
    }
}

static vmav_status_t inner_returns_err(void) {
    return VMAV_ERR(VMAV_ERR_NOT_FOUND, "no such thing");
}

static vmav_status_t outer_uses_try(void) {
    VMAV_TRY(inner_returns_err());
    /* unreachable when inner returns error */
    return VMAV_OK_STATUS;
}

static void test_try_propagates_error(void) {
    vmav_status_t st = outer_uses_try();
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_NOT_FOUND, st.code);
    TEST_ASSERT_NOT_NULL(strstr(st.msg, "no such thing"));
}

static vmav_status_t pass_through(vmav_status_t s) {
    VMAV_TRY(s);
    return VMAV_OK_STATUS;
}

static void test_try_passes_through_ok(void) {
    vmav_status_t result = pass_through(VMAV_OK_STATUS);
    TEST_ASSERT_EQUAL_INT(VMAV_OK, result.code);
}

static void test_message_truncated_safely(void) {
    /* A message larger than msg buffer must truncate without overrun. */
    char big[1024];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    vmav_status_t st = VMAV_ERR(VMAV_ERR_GENERIC, "%s", big);
    TEST_ASSERT_EQUAL_CHAR('\0', st.msg[sizeof(st.msg) - 1]);
    TEST_ASSERT_TRUE(strlen(st.msg) + 1 <= sizeof(st.msg));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ok_status_is_zero);
    RUN_TEST(test_err_macro_captures_location);
    RUN_TEST(test_code_str_covers_every_code);
    RUN_TEST(test_try_propagates_error);
    RUN_TEST(test_try_passes_through_ok);
    RUN_TEST(test_message_truncated_safely);
    return UNITY_END();
}
