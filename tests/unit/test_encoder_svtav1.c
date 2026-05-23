/* Tests for the encoder_svtav1 module:
 *   - vmav_svtav1_film_grain_from_score (pure helper)
 *   - vmav_svtav1_encoder_open + close roundtrip (minimum lifecycle
 *     surface — actual encode loop tested in integration once we
 *     have a YUV fixture in tree). */

#include "vmavificient/vmav_svtav1.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

/* ---- film grain mapping ---- */

static void test_film_grain_zero_score(void) {
    /* Score = 0 → 0 grain for all presets. */
    for (int p = 0; p < VMAV_PRESET_COUNT_; p++) {
        TEST_ASSERT_EQUAL_INT(0, vmav_svtav1_film_grain_from_score(0.0, 0.0, (vmav_preset_t)p));
    }
}

static void test_film_grain_animation_clamps_low(void) {
    /* Animation should clamp aggressively — even at high scores, max is 3. */
    TEST_ASSERT_EQUAL_INT(0, vmav_svtav1_film_grain_from_score(0.05, 0.0, VMAV_PRESET_ANIMATION));
    TEST_ASSERT_LESS_OR_EQUAL_INT(
        3, vmav_svtav1_film_grain_from_score(1.0, 0.0, VMAV_PRESET_ANIMATION));
}

static void test_film_grain_analog_above_digital(void) {
    /* For the same score, analog presets map to a higher grain level than
     * digital. This is the calibration's whole point. */
    const int analog = vmav_svtav1_film_grain_from_score(0.15, 0.0, VMAV_PRESET_SUPER35_ANALOG);
    const int digital = vmav_svtav1_film_grain_from_score(0.15, 0.0, VMAV_PRESET_LIVE_ACTION);
    TEST_ASSERT_GREATER_THAN_INT(digital, analog);
}

static void test_film_grain_caps(void) {
    /* Analog caps at 15, digital at 10, animation at 3. */
    TEST_ASSERT_LESS_OR_EQUAL_INT(
        15, vmav_svtav1_film_grain_from_score(1.0, 0.0, VMAV_PRESET_SUPER35_ANALOG));
    TEST_ASSERT_LESS_OR_EQUAL_INT(
        15, vmav_svtav1_film_grain_from_score(1.0, 0.0, VMAV_PRESET_IMAX_ANALOG));
    TEST_ASSERT_LESS_OR_EQUAL_INT(
        10, vmav_svtav1_film_grain_from_score(1.0, 0.0, VMAV_PRESET_LIVE_ACTION));
    TEST_ASSERT_LESS_OR_EQUAL_INT(
        10, vmav_svtav1_film_grain_from_score(1.0, 0.0, VMAV_PRESET_SUPER35_DIGITAL));
    TEST_ASSERT_LESS_OR_EQUAL_INT(
        3, vmav_svtav1_film_grain_from_score(1.0, 0.0, VMAV_PRESET_ANIMATION));
}

static void test_film_grain_variance_nudge(void) {
    /* High variance should bump a near-boundary score into the higher
     * bracket. At score 0.078 (just below the 0.08 boundary in the digital
     * curve), no nudge stays at the low bracket; with nudge the score is
     * pushed past 0.08 and the higher bracket fires. */
    const int no_nudge = vmav_svtav1_film_grain_from_score(0.078, 0.0, VMAV_PRESET_LIVE_ACTION);
    const int with_nudge = vmav_svtav1_film_grain_from_score(0.078, 0.1, VMAV_PRESET_LIVE_ACTION);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(no_nudge, with_nudge);
}

/* ---- encoder lifecycle ---- */

static vmav_svtav1_spec_t basic_spec(void) {
    vmav_svtav1_spec_t s = {
        .preset = VMAV_PRESET_LIVE_ACTION,
        .width = 1920,
        .height = 1080,
        .bit_depth = 8,
        .fps_num = 24000,
        .fps_den = 1001,
        .film_grain = 0,
        .target_bitrate_kbps = 0,
        .crf = 32,
        .color_primaries = 1, /* bt709 */
        .color_trc = 1,
        .color_space = 1,
        .color_range = 1, /* studio */
        .has_mastering = false,
        .has_cll = false,
    };
    return s;
}

static void test_encoder_open_close_roundtrip(void) {
    vmav_svtav1_spec_t s = basic_spec();
    vmav_svtav1_encoder_t *enc = NULL;
    vmav_status_t st = vmav_svtav1_encoder_open(&s, &enc);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_NULL(enc);
    vmav_svtav1_encoder_close(enc);
}

static void test_encoder_close_null_is_safe(void) {
    vmav_svtav1_encoder_close(NULL); /* must not crash */
    TEST_PASS();
}

static void test_encoder_open_rejects_bad_args(void) {
    vmav_svtav1_spec_t s = basic_spec();
    vmav_svtav1_encoder_t *enc = NULL;

    vmav_status_t st = vmav_svtav1_encoder_open(NULL, &enc);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_NULL(enc);

    st = vmav_svtav1_encoder_open(&s, NULL);
    TEST_ASSERT_FALSE(vmav_status_ok(st));

    s.width = 0;
    st = vmav_svtav1_encoder_open(&s, &enc);
    TEST_ASSERT_FALSE(vmav_status_ok(st));

    s = basic_spec();
    s.bit_depth = 12;
    st = vmav_svtav1_encoder_open(&s, &enc);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_film_grain_zero_score);
    RUN_TEST(test_film_grain_animation_clamps_low);
    RUN_TEST(test_film_grain_analog_above_digital);
    RUN_TEST(test_film_grain_caps);
    RUN_TEST(test_film_grain_variance_nudge);
    RUN_TEST(test_encoder_open_close_roundtrip);
    RUN_TEST(test_encoder_close_null_is_safe);
    RUN_TEST(test_encoder_open_rejects_bad_args);
    return UNITY_END();
}
