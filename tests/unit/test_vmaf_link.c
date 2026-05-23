/* Smoke test: vendored libvmaf links statically.
 * Exercises a tiny slice of the libvmaf C API so missing symbols are
 * caught at link time, not when encoder_vmaf lands. The real measure
 * loop needs reference + distorted frames and a model load — deferred
 * to integration tests. */

#include "unity.h"

#include <libvmaf/libvmaf.h>

#include <stddef.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_vmaf_init_and_close(void) {
    /* vmaf_init creates a VmafContext; vmaf_close releases it. No
     * frames touched, no model loaded — minimum surface that
     * exercises symbol resolution. */
    VmafContext *ctx = NULL;
    VmafConfiguration cfg = {
        .log_level = VMAF_LOG_LEVEL_NONE,
        .n_threads = 1,
        .n_subsample = 1,
        .cpumask = 0,
    };
    const int rc = vmaf_init(&ctx, cfg);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(ctx);

    const char *v = vmaf_version();
    TEST_ASSERT_NOT_NULL(v);

    vmaf_close(ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vmaf_init_and_close);
    return UNITY_END();
}
