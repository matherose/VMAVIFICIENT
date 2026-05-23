/* tests/integration/it_encode_smoke.c
 *
 * End-to-end smoke of the `vmavificient encode` CLI on a tiny fixture.
 * Skips CRF search (`--crf 50`) so the test runs in ~2-3 sec and
 * exercises the full orchestration: probe → tracks_probe → video_encode
 * (SVT-AV1-HDR) → final_mux (ffmpeg subprocess). The fixture has no
 * audio or subtitle tracks (it's a Y4M), so the audio/PGS/RPU branches
 * of cmd_encode are taken but no-op.
 *
 * What we don't test here:
 *   * CRF search (covered by unit test_crf_search with deterministic
 *     curve, and by the strong-mode repro check across encode runs).
 *   * Audio + subtitle path (it_audio_smoke / it_pgs_to_srt in m3).
 *   * DV RPU attachment (still deferred per cmd_encode comment).
 *
 * Failure path discipline: a failed encode dumps the captured stderr
 * so CI shows what cmd_encode actually printed instead of a bare
 * "exit code 1". */

#include "vmavificient/vmav_media.h"
#include "vmavificient/vmav_result.h"
#include "vmavificient/vmav_subproc.h"

#include "unity.h"

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

#ifndef VMAV_FIXTURE_DIR
#error "VMAV_FIXTURE_DIR must be set by vmav_add_integration_test()"
#endif
#ifndef VMAV_BIN
#error "VMAV_BIN must be set by vmav_add_integration_test()"
#endif

static char g_workdir[1024];
static char g_input[1024];
static char g_output[1024];

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

/* Cross-platform replacement for POSIX `mkdtemp` — MinGW's CRT
 * (msvcrt) doesn't provide it. Builds a unique path under $TMPDIR /
 * $TEMP and retries on collision (PID + counter). Returns 0 on
 * success, -1 if no slot worked in 100 attempts. */
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
        snprintf(buf, cap, "%s/vmav_it_encode_smoke_%u_%d", base, (unsigned)getpid(), i);
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

void setUp(void) {
    /* Each test starts in a fresh per-PID workdir so a flaky run never
     * leaks state into the next one. We don't bother cleaning up in
     * tearDown — both /tmp on Linux/macOS and %TEMP% on Windows get
     * cleaned by the OS, and the workdir name includes the PID so
     * concurrent test runs don't collide. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, make_workdir(g_workdir, sizeof(g_workdir)), "make_workdir");
    snprintf(g_input, sizeof(g_input), "%s/in.y4m", g_workdir);
    snprintf(g_output, sizeof(g_output), "%s/out.mkv", g_workdir);
    /* `cmd_encode` writes its `.av1.ivf` intermediate next to the input
     * (via `replace_extension`), so we copy the read-only fixture into
     * our writable workdir first. */
    copy_file(VMAV_FIXTURE_DIR "/tiny.y4m", g_input);
}

void tearDown(void) {
    /* Intentionally empty — see setUp commentary on cleanup. */
}

static void test_encodes_y4m_to_av1_mkv(void) {
    const char *argv[] = {
        VMAV_BIN,
        "encode",
        g_input,
        "--crf",
        "50",
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, res.exit_code, "vmavificient encode failed");
    vmav_subproc_result_free(&res);

    /* Output present + non-empty. */
    struct stat sb;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stat(g_output, &sb), g_output);
    TEST_ASSERT_GREATER_THAN_INT64(0, (int64_t)sb.st_size);

    /* Container + codec + dimensions. The container_name string varies
     * between matroska builds ("matroska,webm" on most FFmpegs); just
     * check the substring rather than exact-match. */
    vmav_media_info_t info;
    const vmav_status_t pst = vmav_media_probe(g_output, &info);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(pst), pst.msg);
    TEST_ASSERT_EQUAL_STRING("av1", info.codec_name);
    TEST_ASSERT_EQUAL_INT(320, info.width);
    TEST_ASSERT_EQUAL_INT(192, info.height);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(info.container_name, "matroska"), info.container_name);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encodes_y4m_to_av1_mkv);
    return UNITY_END();
}
