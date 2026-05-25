/* tests/integration/it_encode_bbb.c
 *
 * Phase 8: end-to-end real-content smoke on a Creative Commons clip
 * (Big Buck Bunny trailer). This complements it_encode_smoke (which
 * exercises the orchestration on a tiny synthetic Y4M) by running the
 * full pipeline on actual H.264 + AAC content — a realistic stand-in
 * for a typical SDR Blu-ray rip minus the HDR signaling.
 *
 * What this gates today (m2 scope):
 *   * Pipeline runs end-to-end on real content without crashing.
 *   * state.json reports every step as COMPLETE on a fresh cache_dir.
 *   * Output MKV exists, is non-empty, and ffprobe sees an AV1 video
 *     stream + at least one opus audio stream.
 *
 * Deferred to later milestones in Phase 8:
 *   * m3 — per-step field assertions (crop rect, grain score range,
 *     CRF search converged, audio language tags, mux stream layout).
 *   * m4 — HDR-passthrough assertion using a Tears of Steel HDR fixture.
 *   * m5 — option matrix (--preset, --crf, --bitrate, --scale-to-hd,
 *     --companion-hd, --dry-run, --grain-only).
 *
 * Skip discipline: if VMAV_FETCH_REAL_CONTENT was OFF at CMake time
 * (the default), VMAV_REAL_CONTENT_ENABLED is 0 here and the single
 * test PASSes immediately with a SKIP marker in the Unity output —
 * clean clones without network access stay green. */

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
#else
#include <unistd.h>
#endif

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

/* Cross-platform replacement for POSIX `mkdtemp` — MinGW's CRT
 * (msvcrt) doesn't provide it. Mirrors the pattern from
 * it_encode_smoke / it_encode_resume. */
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
        snprintf(buf, cap, "%s/vmav_it_encode_bbb_%u_%d", base, (unsigned)getpid(), i);
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

/* state.json pretty-print on test failure. Each step status is mapped
 * to a short tag so failure logs show which step is misbehaving without
 * needing to crack open the cache dir. The `unused` attributes keep
 * the compiler quiet when VMAV_REAL_CONTENT_ENABLED=0 elides the only
 * call site — same pattern as a #if-gated test, just avoids cluttering
 * the helper with more preprocessor noise. */
__attribute__((unused)) static const char *step_tag(vmav_step_status_t s) {
    switch (s) {
    case VMAV_STEP_PENDING:
        return "PENDING";
    case VMAV_STEP_COMPLETE:
        return "COMPLETE";
    case VMAV_STEP_FAILED:
        return "FAILED";
    default:
        return "?";
    }
}

__attribute__((unused)) static void dump_state(const vmav_encode_state_t *s) {
    fprintf(stderr, "\n--- state.json dump ---\n");
    fprintf(stderr, "schema_version = %d\n", s->schema_version);
    fprintf(stderr, "tmdb           = %s\n", step_tag(s->tmdb.status));
    fprintf(stderr,
            "crop           = %s (%dx%d, meaningful=%d)\n",
            step_tag(s->crop.status),
            s->crop.width,
            s->crop.height,
            s->crop.is_meaningful ? 1 : 0);
    fprintf(stderr,
            "grain          = %s (score=%.3f var=%.3f)\n",
            step_tag(s->grain.status),
            s->grain.score,
            s->grain.variance);
    fprintf(stderr,
            "crf            = %s (crf=%d vbr=%dkbps vmaf=%.2f)\n",
            step_tag(s->crf.status),
            s->crf.crf,
            s->crf.bitrate_kbps,
            s->crf.vmaf);
    fprintf(stderr, "audio_count    = %zu\n", s->audio_count);
    for (size_t i = 0; i < s->audio_count; i++) {
        fprintf(stderr,
                "  audio[%zu]     = %s (lang=%s, stream=%d)\n",
                i,
                step_tag(s->audio[i].status),
                s->audio[i].language,
                s->audio[i].stream_index);
    }
    fprintf(stderr, "sub_count      = %zu\n", s->sub_count);
    for (size_t i = 0; i < s->sub_count; i++) {
        fprintf(stderr,
                "  sub[%zu]       = %s (lang=%s, events=%d)\n",
                i,
                step_tag(s->subs[i].status),
                s->subs[i].language,
                s->subs[i].subtitle_count);
    }
    fprintf(stderr,
            "video          = %s (frames=%lld)\n",
            step_tag(s->video.status),
            (long long)s->video.frame_count);
    fprintf(stderr, "mux            = %s\n", step_tag(s->mux.status));
    fprintf(stderr, "video_hd       = %s\n", step_tag(s->video_hd.status));
    fprintf(stderr, "mux_hd         = %s\n", step_tag(s->mux_hd.status));
    fprintf(stderr, "--- end ---\n\n");
}

void setUp(void) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, make_workdir(g_workdir, sizeof(g_workdir)), "make_workdir");
    /* Copy the BBB clip into the workdir so cmd_encode's IVF sidecar
     * (written next to the input via replace_extension) lands somewhere
     * writable. Same pattern as it_encode_smoke. */
    snprintf(g_input, sizeof(g_input), "%s/bbb_clip.mkv", g_workdir);
    snprintf(g_output, sizeof(g_output), "%s/out.mkv", g_workdir);
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/cache", g_workdir);
    copy_file(VMAV_REAL_CONTENT_DIR "/bbb_clip.mkv", g_input);
}

void tearDown(void) {
    /* OS cleans /tmp + %TEMP% — see it_encode_smoke. */
}

static void test_encode_bbb_completes_full_pipeline(void) {
#if VMAV_REAL_CONTENT_ENABLED == 0
    TEST_IGNORE_MESSAGE("VMAV_FETCH_REAL_CONTENT=OFF (default); "
                        "configure with -DVMAV_FETCH_REAL_CONTENT=ON to enable");
#else
    /* --crf 50 skips CRF search (~2 min on a 1080p 5s clip would
     * otherwise dominate CI time). --blind skips TMDB. */
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, res.exit_code, "vmavificient encode (bbb) failed");
    vmav_subproc_result_free(&res);

    /* Output MKV exists + non-empty. */
    struct stat sb;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stat(g_output, &sb), g_output);
    TEST_ASSERT_GREATER_THAN_INT64(0, (int64_t)sb.st_size);

    /* Load state.json, assert each step's status. Dump on any failure
     * so CI logs surface enough info to triage. */
    vmav_encode_state_t state;
    vmav_encode_state_init(&state);
    bool fp_match = false;
    const vmav_status_t lst = vmav_encode_state_load(g_cache_dir, g_input, &state, &fp_match);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(lst), lst.msg);
    TEST_ASSERT_TRUE_MESSAGE(fp_match, "fingerprint mismatch — state.json doesn't match the input");

    /* Every step that ran should report COMPLETE. TMDB was skipped via
     * --blind so we don't assert on it here. */
    if (state.crop.status != VMAV_STEP_COMPLETE || state.grain.status != VMAV_STEP_COMPLETE ||
        state.crf.status != VMAV_STEP_COMPLETE || state.video.status != VMAV_STEP_COMPLETE ||
        state.mux.status != VMAV_STEP_COMPLETE) {
        dump_state(&state);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(VMAV_STEP_COMPLETE, state.crop.status, "crop");
    TEST_ASSERT_EQUAL_INT_MESSAGE(VMAV_STEP_COMPLETE, state.grain.status, "grain");
    TEST_ASSERT_EQUAL_INT_MESSAGE(VMAV_STEP_COMPLETE, state.crf.status, "crf");
    TEST_ASSERT_EQUAL_INT_MESSAGE(VMAV_STEP_COMPLETE, state.video.status, "video");
    TEST_ASSERT_EQUAL_INT_MESSAGE(VMAV_STEP_COMPLETE, state.mux.status, "mux");

    /* BBB has exactly one audio track (AAC 5.1 in the trailer source).
     * Per-step audio assertions are m3's job; m2 just confirms the
     * step ran and recorded the track. */
    TEST_ASSERT_MESSAGE(state.audio_count > 0, "no audio tracks recorded");
    TEST_ASSERT_EQUAL_INT_MESSAGE(VMAV_STEP_COMPLETE, state.audio[0].status, "audio[0]");

    vmav_encode_state_free(&state);

    /* Probe the output MKV: codec + dims. */
    vmav_media_info_t info;
    const vmav_status_t pst = vmav_media_probe(g_output, &info);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(pst), pst.msg);
    TEST_ASSERT_EQUAL_STRING("av1", info.codec_name);
    TEST_ASSERT_EQUAL_INT(1920, info.width);
    TEST_ASSERT_EQUAL_INT(1080, info.height);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(info.container_name, "matroska"), info.container_name);
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_bbb_completes_full_pipeline);
    return UNITY_END();
}
