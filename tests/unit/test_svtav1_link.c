/* Smoke test: vendored SVT-AV1-HDR links statically.
 * Exercises tiny slices of the libSvtAv1Enc C API so missing symbols
 * are caught at link time, not when encoder_svtav1 lands. Real encode
 * round-trips need a YUV fixture; deferred to integration tests. */

#include "unity.h"

#include <stddef.h>

#include <EbSvtAv1Enc.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_svtav1_handle_alloc_and_default_init(void) {
    /* Allocate the encoder handle + default-init its configuration
     * struct. This is the minimum surface that exercises symbol
     * resolution: no input YUV needed, no encode pass actually runs. */
    EbComponentType *handle = NULL;
    EbSvtAv1EncConfiguration cfg = {0};

    const EbErrorType rc_handle = svt_av1_enc_init_handle(&handle, &cfg);
    TEST_ASSERT_EQUAL_INT(EB_ErrorNone, rc_handle);
    TEST_ASSERT_NOT_NULL(handle);

    /* Default config should have sane sentinel values. enc_mode is
     * the SVT-AV1 preset; the default falls anywhere in [0..13]. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, cfg.enc_mode);
    TEST_ASSERT_LESS_OR_EQUAL_INT(13, cfg.enc_mode);

    /* Cleanly release the handle. */
    const EbErrorType rc_deinit = svt_av1_enc_deinit_handle(handle);
    TEST_ASSERT_EQUAL_INT(EB_ErrorNone, rc_deinit);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_svtav1_handle_alloc_and_default_init);
    return UNITY_END();
}
