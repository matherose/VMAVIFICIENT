/* Smoke test: verifies the vendored zlib links statically and a
 * trivial deflate/inflate round-trip works. Phase 3 m2 produces no
 * user-facing functionality yet; this test guards against the
 * cross-compile + static-link pipeline silently degrading. */

#include "unity.h"

#include <string.h>
#include <zlib.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_zlib_version_resolves(void) {
    const char *v = zlibVersion();
    TEST_ASSERT_NOT_NULL(v);
    /* zlib 1.3.1 — first two chars are "1." regardless of patch. */
    TEST_ASSERT_EQUAL_CHAR('1', v[0]);
    TEST_ASSERT_EQUAL_CHAR('.', v[1]);
}

static void test_deflate_inflate_round_trip(void) {
    const char src[] = "vmavificient zlib round-trip — should compress and inflate";
    const uLong src_len = (uLong)strlen(src);

    Bytef compressed[256];
    uLongf comp_len = sizeof(compressed);
    TEST_ASSERT_EQUAL_INT(Z_OK, compress(compressed, &comp_len, (const Bytef *)src, src_len));
    TEST_ASSERT_TRUE(comp_len < sizeof(compressed));

    Bytef restored[256];
    uLongf restored_len = sizeof(restored);
    TEST_ASSERT_EQUAL_INT(Z_OK, uncompress(restored, &restored_len, compressed, comp_len));
    TEST_ASSERT_EQUAL_UINT(src_len, (unsigned)restored_len);
    TEST_ASSERT_EQUAL_STRING_LEN(src, restored, src_len);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zlib_version_resolves);
    RUN_TEST(test_deflate_inflate_round_trip);
    return UNITY_END();
}
