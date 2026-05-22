/* Smoke test: vendored libhdr10plus (Rust + cargo-c) links statically.
 * The crate exposes a JSON-file parser; we feed a nonexistent path
 * so the parser errors out cleanly. Point of the test is to confirm
 * the cbindgen-generated symbols resolve at link time. */

#include "unity.h"

#include <stddef.h>

#include <libhdr10plus-rs/hdr10plus.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_libhdr10plus_parse_missing_file(void) {
    Hdr10PlusRsJsonOpaque *j = hdr10plus_rs_parse_json("/dev/null/nonexistent.json");
    /* parser allocates a handle even on error so callers can fetch the
     * error string; always free what we got. */
    if (j != NULL) {
        (void)hdr10plus_rs_json_get_error(j);
        hdr10plus_rs_json_free(j);
    }
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_libhdr10plus_parse_missing_file);
    return UNITY_END();
}
