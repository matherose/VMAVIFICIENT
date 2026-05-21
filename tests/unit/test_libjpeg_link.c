/* Smoke test: vendored libjpeg-turbo links statically. We exercise the
 * classic libjpeg API (jpeg_std_error + jpeg_CreateCompress) so the
 * linker is forced to resolve real symbols from libjpeg.a. */

#include "unity.h"

#include <jpeglib.h>
#include <stddef.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_libjpeg_error_handler_resolves(void) {
    struct jpeg_error_mgr err;
    /* jpeg_std_error returns a non-NULL pointer (it's `&err`, populated). */
    TEST_ASSERT_EQUAL_PTR(&err, jpeg_std_error(&err));
}

static void test_libjpeg_compress_struct_lifecycle(void) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_compress(&cinfo);
    /* If we get this far without aborting, the symbols are all live. */
    jpeg_destroy_compress(&cinfo);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_libjpeg_error_handler_resolves);
    RUN_TEST(test_libjpeg_compress_struct_lifecycle);
    return UNITY_END();
}
