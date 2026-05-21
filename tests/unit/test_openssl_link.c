/* Smoke test: vendored OpenSSL links statically. Exercises both
 * libssl (TLS context) and libcrypto (RAND, EVP) so missing symbols
 * from either are caught. */

#include "unity.h"

#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_openssl_version_string(void) {
    /* Compile-time constant from <openssl/opensslv.h>. */
    TEST_ASSERT_NOT_NULL(OPENSSL_VERSION_TEXT);
    TEST_ASSERT_NOT_NULL(strstr(OPENSSL_VERSION_TEXT, "OpenSSL"));

    /* Runtime version string from libcrypto. */
    const char *v = OpenSSL_version(OPENSSL_VERSION);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_NOT_NULL(strstr(v, "3.3"));
}

static void test_libcrypto_rand_bytes(void) {
    unsigned char a[32];
    unsigned char b[32];
    TEST_ASSERT_EQUAL_INT(1, RAND_bytes(a, sizeof(a)));
    TEST_ASSERT_EQUAL_INT(1, RAND_bytes(b, sizeof(b)));
    /* Two independent draws should not be byte-identical. (1 in 2^256
     * chance of false fail — practically impossible.) */
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, sizeof(a)));
}

static void test_libcrypto_sha256(void) {
    /* Known answer: SHA-256("abc") =
     *   ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    static const unsigned char want[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL_INT(1, EVP_DigestInit_ex(ctx, EVP_sha256(), NULL));
    TEST_ASSERT_EQUAL_INT(1, EVP_DigestUpdate(ctx, "abc", 3));
    unsigned char got[32];
    unsigned int got_len = 0;
    TEST_ASSERT_EQUAL_INT(1, EVP_DigestFinal_ex(ctx, got, &got_len));
    EVP_MD_CTX_free(ctx);
    TEST_ASSERT_EQUAL_UINT(32, got_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(want, got, 32));
}

static void test_libssl_context_create(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    TEST_ASSERT_NOT_NULL(ctx);
    SSL_CTX_free(ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_openssl_version_string);
    RUN_TEST(test_libcrypto_rand_bytes);
    RUN_TEST(test_libcrypto_sha256);
    RUN_TEST(test_libssl_context_create);
    return UNITY_END();
}
