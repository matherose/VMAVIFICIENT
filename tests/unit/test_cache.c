#include "vmavificient/vmav_cache.h"

#include "vmavificient/vmav_os.h"

#include "unity.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(_WIN32)
#    define vmav_test_setenv(name, val) _putenv_s((name), (val))
#else
#    define vmav_test_setenv(name, val) setenv((name), (val), 1)
#endif

static char g_tmp[256];

void setUp(void) {
    /* Pin the cache dir to a per-process scratch path so tests don't
     * collide with the user's real cache and each test starts clean. */
    snprintf(g_tmp, sizeof(g_tmp), "/tmp/vmav-cache-test-%d-%d", (int)getpid(), rand());
#if defined(__APPLE__)
    /* macOS path_cache_dir reads from HOME (Library/Caches/vmavificient). */
    vmav_test_setenv("HOME", g_tmp);
#else
    vmav_test_setenv("XDG_CACHE_HOME", g_tmp);
#endif
    /* Best-effort cleanup before the test runs. */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tmp);
    (void)system(cmd);
}

void tearDown(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tmp);
    (void)system(cmd);
}

static void test_put_then_get(void) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "score", 0.42);
    cJSON_AddNumberToObject(data, "variance", 0.001);

    vmav_status_t st = vmav_cache_put(VMAV_CACHE_KIND_GRAIN_SCORE, "abc123", data);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    cJSON_Delete(data);

    struct cJSON *read = NULL;
    st = vmav_cache_get(VMAV_CACHE_KIND_GRAIN_SCORE, "abc123", &read);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_NULL(read);

    const cJSON *score = cJSON_GetObjectItemCaseSensitive(read, "score");
    TEST_ASSERT_NOT_NULL(score);
    TEST_ASSERT_TRUE(cJSON_IsNumber(score));
    TEST_ASSERT_EQUAL_DOUBLE(0.42, score->valuedouble);

    vmav_cache_release(read);
}

static void test_get_missing_returns_not_found(void) {
    struct cJSON *read = NULL;
    vmav_status_t st = vmav_cache_get(VMAV_CACHE_KIND_GRAIN_SCORE, "doesnotexist", &read);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_NOT_FOUND, st.code);
    TEST_ASSERT_NULL(read);
}

static void test_invalid_key_chars_rejected(void) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "x", 1);
    vmav_status_t st = vmav_cache_put(VMAV_CACHE_KIND_GRAIN_SCORE, "a/b/c", data);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_cache_put(VMAV_CACHE_KIND_GRAIN_SCORE, "..\\evil", data);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_cache_put(VMAV_CACHE_KIND_GRAIN_SCORE, "..", data);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    cJSON_Delete(data);
}

static void test_different_kinds_isolated(void) {
    cJSON *grain = cJSON_CreateObject();
    cJSON_AddStringToObject(grain, "tag", "grain-value");
    cJSON *crf = cJSON_CreateObject();
    cJSON_AddStringToObject(crf, "tag", "crf-value");

    TEST_ASSERT_TRUE(vmav_status_ok(vmav_cache_put(VMAV_CACHE_KIND_GRAIN_SCORE, "k", grain)));
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_cache_put(VMAV_CACHE_KIND_CRF_SEARCH, "k", crf)));
    cJSON_Delete(grain);
    cJSON_Delete(crf);

    struct cJSON *r1 = NULL;
    struct cJSON *r2 = NULL;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_cache_get(VMAV_CACHE_KIND_GRAIN_SCORE, "k", &r1)));
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_cache_get(VMAV_CACHE_KIND_CRF_SEARCH, "k", &r2)));

    const cJSON *t1 = cJSON_GetObjectItemCaseSensitive(r1, "tag");
    const cJSON *t2 = cJSON_GetObjectItemCaseSensitive(r2, "tag");
    TEST_ASSERT_EQUAL_STRING("grain-value", t1->valuestring);
    TEST_ASSERT_EQUAL_STRING("crf-value", t2->valuestring);
    vmav_cache_release(r1);
    vmav_cache_release(r2);
}

static void test_overwrite_updates_value(void) {
    cJSON *v1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(v1, "v", 1);
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_cache_put(VMAV_CACHE_KIND_PROBE, "key", v1)));
    cJSON_Delete(v1);

    cJSON *v2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(v2, "v", 2);
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_cache_put(VMAV_CACHE_KIND_PROBE, "key", v2)));
    cJSON_Delete(v2);

    struct cJSON *r = NULL;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_cache_get(VMAV_CACHE_KIND_PROBE, "key", &r)));
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(r, "v");
    TEST_ASSERT_EQUAL_INT(2, (int)v->valuedouble);
    vmav_cache_release(r);
}

static void test_kind_str_stable(void) {
    TEST_ASSERT_EQUAL_STRING("grain_score", vmav_cache_kind_str(VMAV_CACHE_KIND_GRAIN_SCORE));
    TEST_ASSERT_EQUAL_STRING("crf_search", vmav_cache_kind_str(VMAV_CACHE_KIND_CRF_SEARCH));
    TEST_ASSERT_EQUAL_STRING("probe", vmav_cache_kind_str(VMAV_CACHE_KIND_PROBE));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_put_then_get);
    RUN_TEST(test_get_missing_returns_not_found);
    RUN_TEST(test_invalid_key_chars_rejected);
    RUN_TEST(test_different_kinds_isolated);
    RUN_TEST(test_overwrite_updates_value);
    RUN_TEST(test_kind_str_stable);
    return UNITY_END();
}
