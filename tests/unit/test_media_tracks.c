/* Tests for vmav_media_tracks_probe. No real-fixtures yet, so error
 * paths only. The track-selection ranking + French-variant detection
 * (Phase 4) will get golden-input tests once we have tiny .mkv
 * fixtures with known audio/sub streams. */

#include "vmavificient/vmav_tracks.h"

#include "unity.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_null_arg_errors(void) {
    vmav_media_tracks_t t;
    vmav_status_t st = vmav_media_tracks_probe(NULL, &t);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_media_tracks_probe("/dev/null", NULL);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

static void test_nonexistent_file(void) {
    vmav_media_tracks_t t;
    vmav_status_t st = vmav_media_tracks_probe("/nonexistent/forsure.mkv", &t);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    /* Free even on failure — struct is zero-init'd. */
    vmav_media_tracks_free(&t);
}

static void test_free_is_idempotent(void) {
    vmav_media_tracks_t t;
    memset(&t, 0, sizeof(t));
    vmav_media_tracks_free(&t);
    vmav_media_tracks_free(&t);
    vmav_media_tracks_free(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null_arg_errors);
    RUN_TEST(test_nonexistent_file);
    RUN_TEST(test_free_is_idempotent);
    return UNITY_END();
}
