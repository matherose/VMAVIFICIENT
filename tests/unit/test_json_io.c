#include "util/json_io.h"

#include "unity.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_path[256];

void setUp(void) {
    snprintf(g_path, sizeof(g_path), "/tmp/vmav-json-test-%d-%d.json", (int)getpid(), rand());
    (void)remove(g_path);
}

void tearDown(void) {
    (void)remove(g_path);
}

static void test_write_and_read_round_trip(void) {
    cJSON *root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    cJSON_AddStringToObject(root, "label", "encoding");
    cJSON_AddNumberToObject(root, "vmaf", 93.5);
    cJSON_AddBoolToObject(root, "done", 1);

    vmav_status_t st = vmav_json_write_atomic(g_path, root);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    cJSON_Delete(root);

    struct cJSON *read = NULL;
    st = vmav_json_read_file(g_path, &read);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_NULL(read);

    TEST_ASSERT_EQUAL_STRING("encoding", vmav_json_get_string(read, "label", ""));
    TEST_ASSERT_EQUAL_DOUBLE(93.5, vmav_json_get_number(read, "vmaf", 0));
    TEST_ASSERT_TRUE(vmav_json_get_bool(read, "done", false));

    vmav_json_free(read);
}

static void test_read_missing_file(void) {
    struct cJSON *root = NULL;
    vmav_status_t st = vmav_json_read_file("/nonexistent/path/forsure.json", &root);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_NULL(root);
}

static void test_parse_invalid_json(void) {
    FILE *fp = fopen(g_path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("{not valid json", fp);
    fclose(fp);

    struct cJSON *root = NULL;
    vmav_status_t st = vmav_json_read_file(g_path, &root);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_PARSE, st.code);
    TEST_ASSERT_NULL(root);
}

static void test_get_returns_default_when_missing(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "present", "yes");
    /* "absent" key not added. */
    TEST_ASSERT_EQUAL_STRING("fallback", vmav_json_get_string(root, "absent", "fallback"));
    TEST_ASSERT_EQUAL_DOUBLE(42.0, vmav_json_get_number(root, "absent", 42.0));
    TEST_ASSERT_FALSE(vmav_json_get_bool(root, "absent", false));
    TEST_ASSERT_EQUAL_INT(17, vmav_json_get_int(root, "absent", 17));
    cJSON_Delete(root);
}

static void test_get_returns_default_on_type_mismatch(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "x", "not-a-number");
    /* Asking for number on a string returns the default. */
    TEST_ASSERT_EQUAL_DOUBLE(99.0, vmav_json_get_number(root, "x", 99.0));
    cJSON_Delete(root);
}

static void test_write_overwrites_existing(void) {
    cJSON *r1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(r1, "v", 1);
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_json_write_atomic(g_path, r1)));
    cJSON_Delete(r1);

    cJSON *r2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(r2, "v", 2);
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_json_write_atomic(g_path, r2)));
    cJSON_Delete(r2);

    struct cJSON *read = NULL;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_json_read_file(g_path, &read)));
    TEST_ASSERT_EQUAL_INT(2, vmav_json_get_int(read, "v", 0));
    vmav_json_free(read);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_and_read_round_trip);
    RUN_TEST(test_read_missing_file);
    RUN_TEST(test_parse_invalid_json);
    RUN_TEST(test_get_returns_default_when_missing);
    RUN_TEST(test_get_returns_default_on_type_mismatch);
    RUN_TEST(test_write_overwrites_existing);
    return UNITY_END();
}
