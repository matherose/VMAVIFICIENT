/* tests/integration/it_encode_hdr.c
 *
 * Phase 8 m4: HDR10 passthrough regression test.
 *
 * Locks the fix from db58919 (mastering display + content light level
 * round-trip through SVT-AV1-HDR's non-spec scale + byteswap). Uses
 * the synthetic HDR fixture from m1+m4's fetch_real_content.sh — a
 * 2-second 3840×2160 HEVC HDR10 test pattern with Display P3
 * primaries, min/max luminance 0.005/4000 cd/m², MaxCLL=1000,
 * MaxFALL=400 in SEI.
 *
 * What this test gates:
 *   * encode runs to completion on UHD HDR input.
 *   * output AV1 bitstream preserves the four pieces of HDR10
 *     metadata, within a small quantization tolerance (SVT's
 *     non-spec scales lose <0.1% on min_luminance and are exact
 *     elsewhere — see project_svt_api_quirks memory).
 *
 * Coverage gap (intentional): we only assert the metadata round-trips.
 * Verifying the PQ transfer is correctly signalled on every frame, or
 * that the actual pixel data is encoded in BT.2020 nonconstant
 * luminance space, is a video-quality concern out of scope for a CI
 * regression test. Real-content QA happens on V_for_Vendetta locally.
 *
 * The metadata extraction uses the same libav side-data API as
 * video_encode.c::probe_hdr_from_first_frame — open + decode 1 frame +
 * harvest AV_FRAME_DATA_MASTERING_DISPLAY_METADATA + AV_FRAME_DATA_
 * CONTENT_LIGHT_LEVEL. Mirroring the implementation pattern means a
 * test failure points at the same code path the encoder uses. */

#include "vmavificient/vmav_media.h"
#include "vmavificient/vmav_result.h"
#include "vmavificient/vmav_subproc.h"

#include "unity.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mastering_display_metadata.h>

#ifndef VMAV_REAL_CONTENT_DIR
#error "VMAV_REAL_CONTENT_DIR must be set by vmav_add_integration_test()"
#endif
#ifndef VMAV_REAL_CONTENT_ENABLED
#error "VMAV_REAL_CONTENT_ENABLED must be set by vmav_add_integration_test()"
#endif
#ifndef VMAV_BIN
#error "VMAV_BIN must be set by vmav_add_integration_test()"
#endif

static char g_workdir[1024];
static char g_input[1024];
static char g_output[1024];
static char g_cache_dir[1024];

static int make_workdir(char *buf, size_t cap) {
    const char *base;
#ifdef _WIN32
    base = getenv("TEMP");
    if (base == NULL || base[0] == '\0') {
        base = ".";
    }
#else
    base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') {
        base = "/tmp";
    }
#endif
    for (int i = 0; i < 100; i++) {
        snprintf(buf, cap, "%s/vmav_it_encode_hdr_%u_%d", base, (unsigned)getpid(), i);
#ifdef _WIN32
        if (_mkdir(buf) == 0) {
            return 0;
        }
#else
        if (mkdir(buf, 0700) == 0) {
            return 0;
        }
#endif
    }
    return -1;
}

static void copy_file(const char *src, const char *dst) {
    FILE *fs = fopen(src, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(fs, src);
    FILE *fd = fopen(dst, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(fd, dst);
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fs)) > 0) {
        const size_t w = fwrite(buf, 1, n, fd);
        TEST_ASSERT_EQUAL_UINT64((uint64_t)n, (uint64_t)w);
    }
    fclose(fs);
    fclose(fd);
}

/* HDR side data extracted from one decoded frame. has_mastering /
 * has_cll == 0 means the file doesn't carry that piece of metadata —
 * the encoder dropped it OR libav couldn't surface it. */
typedef struct {
    bool has_mastering;
    bool has_cll;
    double red_x, red_y;
    double green_x, green_y;
    double blue_x, blue_y;
    double white_x, white_y;
    double max_luma;
    double min_luma;
    uint16_t max_cll;
    uint16_t max_fall;
} hdr_probe_t;

/* Mirror of video_encode.c::probe_hdr_from_first_frame, condensed to
 * the side-data harvesting we care about for assertions. */
__attribute__((unused)) static bool probe_hdr(const char *path, hdr_probe_t *out) {
    memset(out, 0, sizeof(*out));
    AVFormatContext *ifmt = NULL;
    if (avformat_open_input(&ifmt, path, NULL, NULL) < 0) {
        return false;
    }
    bool ok = false;
    if (avformat_find_stream_info(ifmt, NULL) < 0) {
        goto out;
    }
    const int vidx = av_find_best_stream(ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) {
        goto out;
    }
    AVStream *vs = ifmt->streams[vidx];
    const AVCodec *codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (codec == NULL) {
        goto out;
    }
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    if (dec == NULL) {
        goto out;
    }
    if (avcodec_parameters_to_context(dec, vs->codecpar) < 0 ||
        avcodec_open2(dec, codec, NULL) < 0) {
        avcodec_free_context(&dec);
        goto out;
    }
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (pkt == NULL || frame == NULL) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec);
        goto out;
    }
    int packets_pulled = 0;
    while (packets_pulled < 32 && av_read_frame(ifmt, pkt) >= 0) {
        packets_pulled++;
        if (pkt->stream_index != vidx) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(dec, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);
        if (avcodec_receive_frame(dec, frame) == 0) {
            for (int i = 0; i < frame->nb_side_data; i++) {
                const AVFrameSideData *sd = frame->side_data[i];
                if (sd->type == AV_FRAME_DATA_MASTERING_DISPLAY_METADATA) {
                    AVMasteringDisplayMetadata m;
                    memcpy(&m, sd->data, sizeof(m));
                    if (m.has_primaries) {
                        out->has_mastering = true;
                        out->red_x = av_q2d(m.display_primaries[0][0]);
                        out->red_y = av_q2d(m.display_primaries[0][1]);
                        out->green_x = av_q2d(m.display_primaries[1][0]);
                        out->green_y = av_q2d(m.display_primaries[1][1]);
                        out->blue_x = av_q2d(m.display_primaries[2][0]);
                        out->blue_y = av_q2d(m.display_primaries[2][1]);
                        out->white_x = av_q2d(m.white_point[0]);
                        out->white_y = av_q2d(m.white_point[1]);
                    }
                    if (m.has_luminance) {
                        out->has_mastering = true;
                        out->max_luma = av_q2d(m.max_luminance);
                        out->min_luma = av_q2d(m.min_luminance);
                    }
                } else if (sd->type == AV_FRAME_DATA_CONTENT_LIGHT_LEVEL) {
                    AVContentLightMetadata cll;
                    memcpy(&cll, sd->data, sizeof(cll));
                    out->has_cll = true;
                    out->max_cll = (uint16_t)cll.MaxCLL;
                    out->max_fall = (uint16_t)cll.MaxFALL;
                }
            }
            ok = true;
            break;
        }
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&dec);
out:
    avformat_close_input(&ifmt);
    return ok;
}

void setUp(void) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, make_workdir(g_workdir, sizeof(g_workdir)), "make_workdir");
    snprintf(g_input, sizeof(g_input), "%s/hdr_clip.mkv", g_workdir);
    snprintf(g_output, sizeof(g_output), "%s/out.mkv", g_workdir);
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/cache", g_workdir);
    copy_file(VMAV_REAL_CONTENT_DIR "/hdr_clip.mkv", g_input);
}

void tearDown(void) {
}

static void test_hdr10_metadata_round_trips(void) {
#if VMAV_REAL_CONTENT_ENABLED == 0
    TEST_IGNORE_MESSAGE("VMAV_FETCH_REAL_CONTENT=OFF (default); "
                        "configure with -DVMAV_FETCH_REAL_CONTENT=ON to enable");
#else
    /* Sanity: source fixture has the expected HDR side data. If this
     * fails the bug is in fetch_real_content.sh / x265, not vmav. */
    hdr_probe_t src;
    TEST_ASSERT_TRUE_MESSAGE(probe_hdr(g_input, &src), "probe_hdr on source");
    TEST_ASSERT_TRUE_MESSAGE(src.has_mastering, "source missing mastering display");
    TEST_ASSERT_TRUE_MESSAGE(src.has_cll, "source missing content light level");

    /* Encode with --crf 50 to keep the test fast. --blind skips TMDB. */
    const char *argv[] = {
        VMAV_BIN,
        "encode",
        g_input,
        "--blind",
        "--crf",
        "50",
        "--cache-dir",
        g_cache_dir,
        "-o",
        g_output,
        NULL,
    };
    vmav_subproc_spec_t spec = {
        .exe = VMAV_BIN,
        .argv = argv,
        .capture_stderr = true,
        .capture_stdout = true,
    };
    vmav_subproc_result_t res = {0};
    const vmav_status_t st = vmav_subproc_run(&spec, &res);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    if (res.exit_code != 0) {
        const char *err = res.stderr_buf.data != NULL ? (const char *)res.stderr_buf.data : "";
        fprintf(stderr, "\n--- vmavificient stderr ---\n%s\n--- end ---\n", err);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, res.exit_code, "vmavificient encode (hdr) failed");
    vmav_subproc_result_free(&res);

    /* Output exists + has the right codec/dims. */
    struct stat sb;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stat(g_output, &sb), g_output);
    TEST_ASSERT_GREATER_THAN_INT64(0, (int64_t)sb.st_size);
    vmav_media_info_t info;
    const vmav_status_t pst = vmav_media_probe(g_output, &info);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(pst), pst.msg);
    TEST_ASSERT_EQUAL_STRING("av1", info.codec_name);
    TEST_ASSERT_EQUAL_INT(3840, info.width);
    TEST_ASSERT_EQUAL_INT(2160, info.height);

    /* === The actual assertions === */
    hdr_probe_t out;
    TEST_ASSERT_TRUE_MESSAGE(probe_hdr(g_output, &out), "probe_hdr on output");
    TEST_ASSERT_TRUE_MESSAGE(out.has_mastering, "output dropped mastering display");
    TEST_ASSERT_TRUE_MESSAGE(out.has_cll, "output dropped content light level");

    /* Chromaticity tolerance: SVT-AV1-HDR's storage is uint16_t × 65536,
     * one tick ≈ 1.5e-5. The libav decode + libav encode pipeline adds
     * its own rounding (×50000 for the AV1 OBU spec). Allow ±0.0005,
     * which is well beyond the actual drift we observed in db58919
     * (exact match on all eight chromaticity coords for V for Vendetta). */
    const double chroma_tol = 0.0005;
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.red_x, out.red_x, "red_x");
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.red_y, out.red_y, "red_y");
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.green_x, out.green_x, "green_x");
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.green_y, out.green_y, "green_y");
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.blue_x, out.blue_x, "blue_x");
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.blue_y, out.blue_y, "blue_y");
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.white_x, out.white_x, "white_x");
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(chroma_tol, src.white_y, out.white_y, "white_y");

    /* max_luma: SVT's ×256 scale matches exactly at 4000 cd/m². */
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(1.0, src.max_luma, out.max_luma, "max_luma");
    /* min_luma: SVT's ×16384 scale loses ~0.1% at 0.005 cd/m² (one
     * tick = ~6.1e-5). Tolerance bracket is wider than the chroma one. */
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(0.001, src.min_luma, out.min_luma, "min_luma");

    /* CLL: source-native uint16_t, exact match expected. */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(src.max_cll, out.max_cll, "max_cll");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(src.max_fall, out.max_fall, "max_fall");
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hdr10_metadata_round_trips);
    return UNITY_END();
}
