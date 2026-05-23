#include "vmavificient/vmav_preset.h"

#include "config_defaults.h"
#include "unity.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_every_preset_has_info(void) {
    for (int i = 0; i < VMAV_PRESET_COUNT_; i++) {
        const vmav_preset_info_t *info = vmav_preset_info((vmav_preset_t)i);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_NOT_NULL(info->cli_name);
        TEST_ASSERT_NOT_NULL(info->display_name);
        TEST_ASSERT_GREATER_THAN_INT(0, info->vmaf_target_hd);
        TEST_ASSERT_GREATER_THAN_INT(0, info->vmaf_target_4k);
    }
}

static void test_from_string_round_trip(void) {
    for (int i = 0; i < VMAV_PRESET_COUNT_; i++) {
        const vmav_preset_info_t *info = vmav_preset_info((vmav_preset_t)i);
        vmav_preset_t parsed;
        vmav_status_t st = vmav_preset_from_string(info->cli_name, &parsed);
        TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
        TEST_ASSERT_EQUAL_INT(i, parsed);
    }
}

static void test_from_string_accepts_underscore_v1_form(void) {
    vmav_preset_t p;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_preset_from_string("super35_digital", &p)));
    TEST_ASSERT_EQUAL_INT(VMAV_PRESET_SUPER35_DIGITAL, p);
}

static void test_from_string_case_insensitive(void) {
    vmav_preset_t p;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_preset_from_string("LIVE-ACTION", &p)));
    TEST_ASSERT_EQUAL_INT(VMAV_PRESET_LIVE_ACTION, p);
}

static void test_from_string_unknown_errors(void) {
    vmav_preset_t p;
    vmav_status_t st = vmav_preset_from_string("blockbuster", &p);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

static void test_vmaf_target_resolves_by_height(void) {
    /* 4K threshold = 2160. */
    TEST_ASSERT_EQUAL_INT(VMAV_VMAF_LIVE_ACTION_4K,
                          vmav_preset_target_vmaf(VMAV_PRESET_LIVE_ACTION, 2160));
    TEST_ASSERT_EQUAL_INT(VMAV_VMAF_LIVE_ACTION_HD,
                          vmav_preset_target_vmaf(VMAV_PRESET_LIVE_ACTION, 1080));
    TEST_ASSERT_EQUAL_INT(VMAV_VMAF_IMAX_HD,
                          vmav_preset_target_vmaf(VMAV_PRESET_IMAX_DIGITAL, 720));
}

static void test_bitrate_picks_grainy_tier_above_threshold(void) {
    /* Live-action HD grainy = 2500 kbps. */
    const int bps = vmav_preset_target_bitrate(VMAV_PRESET_LIVE_ACTION, 1080, 0.5);
    TEST_ASSERT_EQUAL_INT(VMAV_BITRATE_HD_GRAINY, bps);
}

static void test_bitrate_picks_clean_tier_below_threshold(void) {
    /* Live-action HD clean = 2000 kbps. */
    const int bps = vmav_preset_target_bitrate(VMAV_PRESET_LIVE_ACTION, 1080, 0.0);
    TEST_ASSERT_EQUAL_INT(VMAV_BITRATE_HD_CLEAN, bps);
}

static void test_bitrate_animation_flat(void) {
    /* Animation ignores grain_score. */
    TEST_ASSERT_EQUAL_INT(VMAV_BITRATE_HD_ANIMATION,
                          vmav_preset_target_bitrate(VMAV_PRESET_ANIMATION, 1080, 0.0));
    TEST_ASSERT_EQUAL_INT(VMAV_BITRATE_HD_ANIMATION,
                          vmav_preset_target_bitrate(VMAV_PRESET_ANIMATION, 1080, 1.0));
    TEST_ASSERT_EQUAL_INT(VMAV_BITRATE_4K_ANIMATION,
                          vmav_preset_target_bitrate(VMAV_PRESET_ANIMATION, 2160, 0.5));
}

static void test_super35_analog_uses_film_grain_mode(void) {
    const vmav_preset_info_t *info = vmav_preset_info(VMAV_PRESET_SUPER35_ANALOG);
    TEST_ASSERT_EQUAL_INT(VMAV_GRAIN_FILM, info->grain_mode);
}

static void test_animation_uses_synthetic_grain(void) {
    const vmav_preset_info_t *info = vmav_preset_info(VMAV_PRESET_ANIMATION);
    TEST_ASSERT_EQUAL_INT(VMAV_GRAIN_SYNTHETIC, info->grain_mode);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_preset_has_info);
    RUN_TEST(test_from_string_round_trip);
    RUN_TEST(test_from_string_accepts_underscore_v1_form);
    RUN_TEST(test_from_string_case_insensitive);
    RUN_TEST(test_from_string_unknown_errors);
    RUN_TEST(test_vmaf_target_resolves_by_height);
    RUN_TEST(test_bitrate_picks_grainy_tier_above_threshold);
    RUN_TEST(test_bitrate_picks_clean_tier_below_threshold);
    RUN_TEST(test_bitrate_animation_flat);
    RUN_TEST(test_super35_analog_uses_film_grain_mode);
    RUN_TEST(test_animation_uses_synthetic_grain);
    return UNITY_END();
}
