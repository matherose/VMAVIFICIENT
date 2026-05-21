/* Smoke test: vendored libtiff links statically. Exercises the
 * library-info APIs so the linker resolves real symbols rather than
 * a single header-only signature. */

#include "unity.h"

#include <string.h>
#include <tiffio.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_libtiff_version_resolves(void) {
    const char *v = TIFFGetVersion();
    TEST_ASSERT_NOT_NULL(v);
    /* v looks like "LIBTIFF, Version 4.6.0\n..." */
    TEST_ASSERT_NOT_NULL(strstr(v, "LIBTIFF"));
    TEST_ASSERT_NOT_NULL(strstr(v, "4."));
}

static void test_libtiff_error_handler_install(void) {
    /* Set and reset the error handler — proves symbols resolve. */
    TIFFErrorHandler prev = TIFFSetErrorHandler(NULL);
    TIFFSetErrorHandler(prev);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_libtiff_version_resolves);
    RUN_TEST(test_libtiff_error_handler_install);
    return UNITY_END();
}
