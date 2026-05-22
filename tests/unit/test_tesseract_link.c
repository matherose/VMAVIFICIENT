/* Smoke test: vendored tesseract + leptonica link statically.
 * Exercises a tiny slice of each library's public C API so missing
 * symbols are caught at link time, not later when subtitle_convert
 * lands. The actual OCR pipeline needs a tessdata fixture; that's
 * deferred to integration tests once we have a sample PGS in tree. */

#include "unity.h"

#include <allheaders.h> /* leptonica */
#include <stddef.h>

#include <tesseract/capi.h> /* tesseract C API */

void setUp(void) {
}

void tearDown(void) {
}

static void test_leptonica_pix_roundtrip(void) {
    /* pixCreate / pixDestroy is the minimal leptonica smoke. If the
     * static lib links at all, this allocates an 8-bit 16x16 image and
     * frees it without touching the filesystem. */
    PIX *p = pixCreate(16, 16, 8);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(16, pixGetWidth(p));
    TEST_ASSERT_EQUAL_INT(16, pixGetHeight(p));
    pixDestroy(&p);
    TEST_ASSERT_NULL(p);
}

static void test_tesseract_api_create_and_version(void) {
    /* TessVersion() returns the build's version string. No tessdata
     * needed — we're just verifying the symbol resolves and the static
     * library initializes. */
    const char *version = TessVersion();
    TEST_ASSERT_NOT_NULL(version);
    TEST_ASSERT_TRUE(version[0] == '5'); /* 5.5.2 */

    /* TessBaseAPICreate / Delete is the no-op equivalent for the OCR
     * API: allocates the engine without loading any model. */
    TessBaseAPI *api = TessBaseAPICreate();
    TEST_ASSERT_NOT_NULL(api);
    TessBaseAPIDelete(api);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_leptonica_pix_roundtrip);
    RUN_TEST(test_tesseract_api_create_and_version);
    return UNITY_END();
}
