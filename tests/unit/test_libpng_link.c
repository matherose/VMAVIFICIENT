/* Smoke test: vendored libpng links statically and exposes its
 * version string. Validates the libpng -> zlib link chain on every
 * triple. */

#include "unity.h"

#include <png.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_libpng_version_resolves(void) {
    const char *v = png_get_libpng_ver(NULL);
    TEST_ASSERT_NOT_NULL(v);
    /* libpng 1.6.x — first three chars are "1.6" */
    TEST_ASSERT_EQUAL_CHAR('1', v[0]);
    TEST_ASSERT_EQUAL_CHAR('.', v[1]);
    TEST_ASSERT_EQUAL_CHAR('6', v[2]);
}

static void test_libpng_can_create_struct(void) {
    /* Exercise the linker more aggressively: allocate and free a
     * png_struct so we know more symbols than the version getter
     * resolved at link time. */
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(p);
    png_destroy_write_struct(&p, NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_libpng_version_resolves);
    RUN_TEST(test_libpng_can_create_struct);
    return UNITY_END();
}
