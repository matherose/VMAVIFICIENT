/* Smoke test: vendored libdovi (Rust + cargo-c) links statically.
 * Exercises the C API entry points so missing symbols from the
 * Rust→C bridge are caught at link time, not runtime. */

#include "unity.h"

#include <stddef.h>

#include <libdovi/rpu_parser.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_libdovi_parse_invalid_rpu(void) {
    /* Feed bogus bytes; we just want to confirm the function symbol
     * resolves and rejects garbage cleanly. */
    static const uint8_t junk[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    DoviRpuOpaque *rpu = dovi_parse_rpu(junk, sizeof(junk));
    /* parser may or may not return NULL on junk — what matters is
     * the symbol resolves at link. If non-NULL, free it. */
    if (rpu != NULL) {
        dovi_rpu_free(rpu);
    }
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_libdovi_parse_invalid_rpu);
    return UNITY_END();
}
