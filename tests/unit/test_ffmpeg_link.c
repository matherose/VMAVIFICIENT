/* Smoke test: vendored FFmpeg links statically and reports versions
 * from each of the libs we ship (avformat, avcodec, avutil, avfilter,
 * swscale, swresample). Verifies no link-time dep is missing across
 * the FFmpeg-internal DAG. */

#include "unity.h"

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_ffmpeg_lib_versions(void) {
    /* Each lib has a version macro at compile time and a getter at
     * runtime; both must agree (FFmpeg's internal sanity check). */
    TEST_ASSERT_EQUAL_UINT(LIBAVUTIL_VERSION_INT, avutil_version());
    TEST_ASSERT_EQUAL_UINT(LIBAVCODEC_VERSION_INT, avcodec_version());
    TEST_ASSERT_EQUAL_UINT(LIBAVFORMAT_VERSION_INT, avformat_version());
    TEST_ASSERT_EQUAL_UINT(LIBAVFILTER_VERSION_INT, avfilter_version());
    TEST_ASSERT_EQUAL_UINT(LIBSWSCALE_VERSION_INT, swscale_version());
    TEST_ASSERT_EQUAL_UINT(LIBSWRESAMPLE_VERSION_INT, swresample_version());
}

static void test_ffmpeg_major_version_is_8(void) {
    /* n8.1.1 implies libavutil major 60 — guards against accidental
     * downgrade if someone bumps the submodule. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(AV_VERSION_INT(60, 0, 0), LIBAVUTIL_VERSION_INT);
}

static void test_ffmpeg_format_context_alloc(void) {
    /* Exercise libavformat's allocator to force link-time symbol
     * resolution beyond the version getter. */
    AVFormatContext *ctx = avformat_alloc_context();
    TEST_ASSERT_NOT_NULL(ctx);
    avformat_free_context(ctx);
}

static void test_ffmpeg_codec_lookup(void) {
    /* Look up a codec that should be unconditionally present. */
    const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_PCM_S16LE);
    TEST_ASSERT_NOT_NULL(c);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ffmpeg_lib_versions);
    RUN_TEST(test_ffmpeg_major_version_is_8);
    RUN_TEST(test_ffmpeg_format_context_alloc);
    RUN_TEST(test_ffmpeg_codec_lookup);
    return UNITY_END();
}
