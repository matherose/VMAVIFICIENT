/* tests/integration/it_encode_resume.c
 *
 * End-to-end test of the Phase 6 resume contract:
 *
 *   1. Run a full encode with `--cache-dir <wd>/cache`. Confirm state
 *      .json reports every step as COMPLETE and the final MKV is on
 *      disk.
 *   2. Knock state.mux back to PENDING (and clear its output_path),
 *      then `remove()` the final MKV. The cache still contains the
 *      IVF intermediate and the state.json says everything before
 *      mux is COMPLETE.
 *   3. Re-run with the same --cache-dir. The expectation:
 *      * exit 0
 *      * IVF intermediate's mtime is unchanged (video step skipped)
 *      * final MKV reappears with the expected stream layout
 *
 * If the second run rebuilds the IVF (i.e. video step ran again), the
 * mtime check catches it — that's a regression of the state-driven
 * skip we just landed in m4. */

#include "vmavificient/vmav_encode_state.h"
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
#include <windows.h>
/* POSIX sleep(seconds) doesn't exist on Windows; map to Sleep(ms). */
#define sleep(s) Sleep((s) * 1000)
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
static char g_cache[1024];
static char g_ivf[1280];

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
        snprintf(buf, cap, "%s/vmav_it_resume_%u_%d", base, (unsigned)getpid(), i);
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

void setUp(void) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, make_workdir(g_workdir, sizeof(g_workdir)), "make_workdir");
    snprintf(g_input, sizeof(g_input), "%s/in.y4m", g_workdir);
    snprintf(g_output, sizeof(g_output), "%s/out.mkv", g_workdir);
    snprintf(g_cache, sizeof(g_cache), "%s/cache", g_workdir);
    snprintf(g_ivf, sizeof(g_ivf), "%s/in.av1.ivf", g_workdir);
    copy_file(VMAV_FIXTURE_DIR "/tiny.y4m", g_input);
}

void tearDown(void) {
}

/* Invoke `vmavificient encode --crf 50 --cache-dir g_cache g_input -o g_output`.
 * On non-zero exit, dump the captured stderr so CI logs show what
 * actually failed. Returns the process exit code. */
static int run_encode(void) {
    const char *argv[] = {
        VMAV_BIN,
        "encode",
        g_input,
        "--crf",
        "50",
        "--cache-dir",
        g_cache,
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
    const int exit_code = res.exit_code;
    if (exit_code != 0) {
        const char *err = res.stderr_buf.data != NULL ? (const char *)res.stderr_buf.data : "";
        fprintf(stderr, "\n--- vmavificient stderr ---\n%s\n--- end ---\n", err);
    }
    vmav_subproc_result_free(&res);
    return exit_code;
}

static int64_t mtime_of(const char *path) {
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return -1;
    }
    return (int64_t)sb.st_mtime;
}

static int64_t size_of(const char *path) {
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return -1;
    }
    return (int64_t)sb.st_size;
}

static void test_resume_skips_cached_steps(void) {
    /* === Phase 1: initial full encode === */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, run_encode(), "initial encode failed");

    /* IVF intermediate must exist non-empty. */
    const int64_t ivf_t_first = mtime_of(g_ivf);
    TEST_ASSERT_GREATER_THAN_INT64_MESSAGE(0, ivf_t_first, g_ivf);
    TEST_ASSERT_GREATER_THAN_INT64(0, size_of(g_ivf));

    /* State must report every step COMPLETE. */
    vmav_encode_state_t state;
    bool fp_match = false;
    TEST_ASSERT_TRUE_MESSAGE(
        vmav_status_ok(vmav_encode_state_load(g_cache, g_input, &state, &fp_match)),
        "state_load failed");
    TEST_ASSERT_TRUE(fp_match);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, state.crop.status);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, state.grain.status);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, state.crf.status);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, state.video.status);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, state.mux.status);

    /* === Phase 2: corrupt the cache as if killed during/after mux === */
    state.mux.status = VMAV_STEP_PENDING;
    state.mux.output_path[0] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(vmav_encode_state_save(g_cache, &state)),
                             "state_save failed");
    vmav_encode_state_free(&state);
    /* Simulate the partial-mux failure: drop the .mkv on disk. */
    remove(g_output);
    TEST_ASSERT_EQUAL_INT64(-1, size_of(g_output));

    /* Note the IVF state so we can verify the video step doesn't
     * re-run. mtime granularity on most fs is 1s — give the test
     * fs a chance to bump it if the video step DOES rewrite the
     * file (we WANT to catch that regression). */
    sleep(1);

    /* === Phase 3: resume === */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, run_encode(), "resume encode failed");

    /* IVF must NOT have been rewritten. */
    const int64_t ivf_t_second = mtime_of(g_ivf);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(ivf_t_first,
                                    ivf_t_second,
                                    "video step rebuilt the IVF on resume "
                                    "(state.video.status check is broken)");

    /* Final MKV must exist + probe back as AV1 in matroska at 320x192. */
    TEST_ASSERT_GREATER_THAN_INT64_MESSAGE(0, size_of(g_output), g_output);
    vmav_media_info_t info;
    const vmav_status_t pst = vmav_media_probe(g_output, &info);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(pst), pst.msg);
    TEST_ASSERT_EQUAL_STRING("av1", info.codec_name);
    TEST_ASSERT_EQUAL_INT(320, info.width);
    TEST_ASSERT_EQUAL_INT(192, info.height);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(info.container_name, "matroska"), info.container_name);

    /* State must once again report every step COMPLETE. */
    fp_match = false;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_load(g_cache, g_input, &state, &fp_match)));
    TEST_ASSERT_TRUE(fp_match);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, state.mux.status);
    vmav_encode_state_free(&state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_resume_skips_cached_steps);
    return UNITY_END();
}
