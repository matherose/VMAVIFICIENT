/* Pure-logic tests for media_naming. Doesn't touch the filesystem or
 * FFmpeg — purely exercises filename parsing + tag computation. */

#include "vmavificient/vmav_naming.h"

#include "unity.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/* === detect_source ============================================ */

static void test_source_bluray(void) {
    TEST_ASSERT_EQUAL_INT(VMAV_SOURCE_BLURAY,
                          vmav_naming_detect_source("Inception.2010.BluRay.1080p.mkv"));
}

static void test_source_remux(void) {
    TEST_ASSERT_EQUAL_INT(VMAV_SOURCE_REMUX, vmav_naming_detect_source("Foo.BDREMUX.2160p.mkv"));
    TEST_ASSERT_EQUAL_INT(VMAV_SOURCE_REMUX, vmav_naming_detect_source("Bar.REMUX.mkv"));
}

static void test_source_webdl_before_web(void) {
    /* WEB-DL must win over WEB. */
    TEST_ASSERT_EQUAL_INT(VMAV_SOURCE_WEBDL,
                          vmav_naming_detect_source("Show.S01E01.WEB-DL.1080p.mkv"));
    TEST_ASSERT_EQUAL_INT(VMAV_SOURCE_WEB, vmav_naming_detect_source("Movie.WEB.mkv"));
}

static void test_source_unknown_fallback(void) {
    TEST_ASSERT_EQUAL_INT(VMAV_SOURCE_UNKNOWN, vmav_naming_detect_source("random_clip.mp4"));
    TEST_ASSERT_EQUAL_INT(VMAV_SOURCE_UNKNOWN, vmav_naming_detect_source(NULL));
}

/* === detect_french_variant ==================================== */

static void test_french_variant_specific_before_generic(void) {
    /* VFQ must be detected before VFF (substring would match VFF first
     * if the order were wrong). */
    TEST_ASSERT_EQUAL_INT(VMAV_FR_VFQ, vmav_naming_detect_french_variant("Movie.VFQ.1080p.mkv"));
    TEST_ASSERT_EQUAL_INT(VMAV_FR_VFI, vmav_naming_detect_french_variant("Movie.VFI.1080p.mkv"));
    TEST_ASSERT_EQUAL_INT(VMAV_FR_VFF, vmav_naming_detect_french_variant("Movie.VFF.1080p.mkv"));
    TEST_ASSERT_EQUAL_INT(VMAV_FR_VFF,
                          vmav_naming_detect_french_variant("Movie.TRUEFRENCH.1080p.mkv"));
    TEST_ASSERT_EQUAL_INT(VMAV_FR_UNKNOWN, vmav_naming_detect_french_variant("Movie.1080p.mkv"));
}

/* === lang_tag computation ===================================== */

static vmav_media_tracks_t make_tracks(const char *langs[], size_t n) {
    static vmav_track_t buf[8];
    memset(buf, 0, sizeof(buf));
    vmav_media_tracks_t t = {0};
    for (size_t i = 0; i < n && i < 8; i++) {
        snprintf(buf[i].language, sizeof(buf[i].language), "%s", langs[i]);
    }
    t.audio = buf;
    t.audio_count = n;
    return t;
}

static void test_lang_tag_fr_only_non_french_movie(void) {
    const char *langs[] = {"fre"};
    vmav_media_tracks_t t = make_tracks(langs, 1);
    TEST_ASSERT_EQUAL_INT(VMAV_LT_VFF, vmav_naming_lang_tag(&t, "en", VMAV_FR_VFF));
    TEST_ASSERT_EQUAL_INT(VMAV_LT_FRENCH, vmav_naming_lang_tag(&t, "en", VMAV_FR_UNKNOWN));
}

static void test_lang_tag_fr_only_french_movie(void) {
    const char *langs[] = {"fre"};
    vmav_media_tracks_t t = make_tracks(langs, 1);
    /* French film with only its original French audio = VO, not VFF. */
    TEST_ASSERT_EQUAL_INT(VMAV_LT_VO, vmav_naming_lang_tag(&t, "fr", VMAV_FR_VFF));
}

static void test_lang_tag_dual_fr_en(void) {
    const char *langs[] = {"fre", "eng"};
    vmav_media_tracks_t t = make_tracks(langs, 2);
    TEST_ASSERT_EQUAL_INT(VMAV_LT_DUAL_VFF, vmav_naming_lang_tag(&t, "en", VMAV_FR_VFF));
    TEST_ASSERT_EQUAL_INT(VMAV_LT_DUAL_VFQ, vmav_naming_lang_tag(&t, "en", VMAV_FR_VFQ));
}

static void test_lang_tag_multi(void) {
    const char *langs[] = {"fre", "eng", "spa", "ita"};
    vmav_media_tracks_t t = make_tracks(langs, 4);
    TEST_ASSERT_EQUAL_INT(VMAV_LT_MULTI_VFF, vmav_naming_lang_tag(&t, "en", VMAV_FR_VFF));
    TEST_ASSERT_EQUAL_INT(VMAV_LT_MULTI, vmav_naming_lang_tag(&t, "en", VMAV_FR_UNKNOWN));
}

static void test_lang_tag_no_french(void) {
    const char *langs[] = {"eng"};
    vmav_media_tracks_t t = make_tracks(langs, 1);
    TEST_ASSERT_EQUAL_INT(VMAV_LT_VO, vmav_naming_lang_tag(&t, "en", VMAV_FR_UNKNOWN));
}

/* === build_output_filename ==================================== */

static void test_build_basic(void) {
    char out[256];
    vmav_hdr_info_t hdr = {0};
    vmav_status_t st = vmav_naming_build(out,
                                         sizeof(out),
                                         "Inception",
                                         2010,
                                         VMAV_LT_MULTI_VFF,
                                         1080,
                                         &hdr,
                                         VMAV_SOURCE_BLURAY,
                                         "MyGroup");
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_NOT_NULL(strstr(out, "Inception.2010"));
    TEST_ASSERT_NOT_NULL(strstr(out, "1080p"));
    TEST_ASSERT_NOT_NULL(strstr(out, "MULTi.VFF"));
    TEST_ASSERT_NOT_NULL(strstr(out, "BluRay"));
    TEST_ASSERT_NOT_NULL(strstr(out, "MyGroup"));
    TEST_ASSERT_NOT_NULL(strstr(out, ".mkv"));
}

static void test_build_hdr_dv_takes_priority(void) {
    char out[256];
    vmav_hdr_info_t hdr = {.has_dolby_vision = true,
                           .has_hdr10 = true,
                           .has_hdr10plus = true,
                           .dv_profile = 7,
                           .dv_level = 6};
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_naming_build(
        out, sizeof(out), "Dune", 2021, VMAV_LT_VO, 2160, &hdr, VMAV_SOURCE_REMUX, "g")));
    TEST_ASSERT_NOT_NULL(strstr(out, ".DV."));
    TEST_ASSERT_NULL(strstr(out, "HDR10"));
}

static void test_build_sanitizes_title(void) {
    char out[256];
    vmav_hdr_info_t hdr = {0};
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_naming_build(out,
                                                      sizeof(out),
                                                      "Spider-Man: Far From Home",
                                                      2019,
                                                      VMAV_LT_VO,
                                                      1080,
                                                      &hdr,
                                                      VMAV_SOURCE_WEBDL,
                                                      "GRP")));
    TEST_ASSERT_NOT_NULL(strstr(out, "Spider.Man.Far.From.Home"));
    TEST_ASSERT_NULL(strstr(out, "  "));
    TEST_ASSERT_NULL(strstr(out, ":"));
}

static void test_build_resolution_tier(void) {
    char out[256];
    vmav_hdr_info_t hdr = {0};
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_naming_build(
        out, sizeof(out), "x", 2020, VMAV_LT_VO, 2160, &hdr, VMAV_SOURCE_BLURAY, "g")));
    TEST_ASSERT_NOT_NULL(strstr(out, "2160p"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_source_bluray);
    RUN_TEST(test_source_remux);
    RUN_TEST(test_source_webdl_before_web);
    RUN_TEST(test_source_unknown_fallback);
    RUN_TEST(test_french_variant_specific_before_generic);
    RUN_TEST(test_lang_tag_fr_only_non_french_movie);
    RUN_TEST(test_lang_tag_fr_only_french_movie);
    RUN_TEST(test_lang_tag_dual_fr_en);
    RUN_TEST(test_lang_tag_multi);
    RUN_TEST(test_lang_tag_no_french);
    RUN_TEST(test_build_basic);
    RUN_TEST(test_build_hdr_dv_takes_priority);
    RUN_TEST(test_build_sanitizes_title);
    RUN_TEST(test_build_resolution_tier);
    return UNITY_END();
}
