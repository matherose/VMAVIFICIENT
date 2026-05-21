/* Smoke test: vendored libcurl links statically against vendored
 * OpenSSL. We don't make a real network call here — that's an
 * integration test for tmdb_client. We just check the version
 * report includes the right backend strings. */

#include "unity.h"

#include <stddef.h>
#include <string.h>

#include <curl/curl.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_libcurl_version_includes_openssl(void) {
    curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_NOT_NULL(info->version);
    TEST_ASSERT_NOT_NULL(info->ssl_version);
    /* Vendored OpenSSL backend; we don't want host BoringSSL/LibreSSL
     * to silently substitute. */
    TEST_ASSERT_NOT_NULL(strstr(info->ssl_version, "OpenSSL"));
}

static void test_libcurl_easy_init_lifecycle(void) {
    CURL *h = curl_easy_init();
    TEST_ASSERT_NOT_NULL(h);

    /* Set a few options that touch different feature areas to force
     * the linker to pull in those code paths. */
    TEST_ASSERT_EQUAL_INT(CURLE_OK, curl_easy_setopt(h, CURLOPT_URL, "https://example.invalid/"));
    TEST_ASSERT_EQUAL_INT(CURLE_OK, curl_easy_setopt(h, CURLOPT_TIMEOUT, 1L));
    TEST_ASSERT_EQUAL_INT(CURLE_OK, curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L));

    curl_easy_cleanup(h);
}

static void test_libcurl_global_init_cleanup(void) {
    /* SSL setup is in here — fails fast if vendored OpenSSL is broken. */
    TEST_ASSERT_EQUAL_INT(CURLE_OK, curl_global_init(CURL_GLOBAL_DEFAULT));
    curl_global_cleanup();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_libcurl_version_includes_openssl);
    RUN_TEST(test_libcurl_easy_init_lifecycle);
    RUN_TEST(test_libcurl_global_init_cleanup);
    return UNITY_END();
}
