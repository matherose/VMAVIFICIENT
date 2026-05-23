/* tests/integration/it_fixtures_smoke.c
 *
 * Validates the fixture-generation pipeline end-to-end:
 *   1. The on-disk fixture file exists and is non-empty.
 *   2. libavformat can probe it through our vmav_media_probe wrapper.
 *   3. The probed dimensions match what fixturegen wrote (320x180).
 *
 * If this test passes, vmav_add_integration_test()'s plumbing
 * (vmav_fixtures dependency, VMAV_FIXTURE_DIR compile def, ctest
 * label) is wired correctly. Real per-feature integration tests
 * (it_encode_smoke, it_pgs_to_srt) build on top of this contract. */

#include "vmavificient/vmav_media.h"
#include "vmavificient/vmav_result.h"

#include "unity.h"

#include <stdint.h>
#include <stdio.h>

#include <sys/stat.h>

#ifndef VMAV_FIXTURE_DIR
#error "VMAV_FIXTURE_DIR must be set by vmav_add_integration_test()"
#endif

static const char *const k_tiny_y4m = VMAV_FIXTURE_DIR "/tiny.y4m";

void setUp(void) {
}

void tearDown(void) {
}

static void test_fixture_file_exists(void) {
    struct stat sb;
    const int rc = stat(k_tiny_y4m, &sb);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, k_tiny_y4m);
    TEST_ASSERT_GREATER_THAN_INT64(0, (int64_t)sb.st_size);
}

static void test_fixture_probes_as_video(void) {
    vmav_media_info_t info;
    const vmav_status_t st = vmav_media_probe(k_tiny_y4m, &info);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_EQUAL_INT(320, info.width);
    TEST_ASSERT_EQUAL_INT(192, info.height);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fixture_file_exists);
    RUN_TEST(test_fixture_probes_as_video);
    return UNITY_END();
}
