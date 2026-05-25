/* Unit tests for vmav_encode_state: status enum round-trips, JSON
 * round-trip across every field, default cache dir derivation,
 * fingerprint mismatch invalidation. The end-to-end "kill mid-encode
 * + resume" coverage lives in the m5 integration test. */

#include "vmavificient/vmav_encode_state.h"
#include "vmavificient/vmav_result.h"

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

void setUp(void) {
}

void tearDown(void) {
}

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
        snprintf(buf, cap, "%s/vmav_state_test_%u_%d", base, (unsigned)getpid(), i);
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

static void write_dummy_input(const char *path, size_t size_bytes) {
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    char *buf = calloc(size_bytes, 1);
    TEST_ASSERT_NOT_NULL(buf);
    /* Make the content distinct so any future hash-based fingerprint
     * doesn't trivially collide across tests. */
    for (size_t i = 0; i < size_bytes; i++) {
        buf[i] = (char)(i & 0xff);
    }
    TEST_ASSERT_EQUAL_UINT64(size_bytes, fwrite(buf, 1, size_bytes, f));
    free(buf);
    fclose(f);
}

/* ---- step_status helpers ---- */

static void test_status_strings_round_trip(void) {
    TEST_ASSERT_EQUAL_STRING("pending", vmav_step_status_str(VMAV_STEP_PENDING));
    TEST_ASSERT_EQUAL_STRING("complete", vmav_step_status_str(VMAV_STEP_COMPLETE));
    TEST_ASSERT_EQUAL_STRING("failed", vmav_step_status_str(VMAV_STEP_FAILED));

    TEST_ASSERT_EQUAL_INT(VMAV_STEP_PENDING, vmav_step_status_from_str("pending"));
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, vmav_step_status_from_str("complete"));
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_FAILED, vmav_step_status_from_str("failed"));
    /* Unknowns default to pending — safer than failed (we'd skip the
     * step) or complete (we'd skip and pretend it worked). */
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_PENDING, vmav_step_status_from_str("nope"));
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_PENDING, vmav_step_status_from_str(NULL));
}

/* ---- default cache dir derivation ---- */

static void test_default_cache_dir(void) {
    char buf[256];
    TEST_ASSERT_TRUE(
        vmav_status_ok(vmav_encode_state_default_cache_dir("/tmp/V_clip.mkv", buf, sizeof(buf))));
    TEST_ASSERT_EQUAL_STRING("/tmp/.vmavificient-cache", buf);

    TEST_ASSERT_TRUE(
        vmav_status_ok(vmav_encode_state_default_cache_dir("relative.mkv", buf, sizeof(buf))));
    TEST_ASSERT_EQUAL_STRING("./.vmavificient-cache", buf);

    /* Overflow: small buffer should fail cleanly. */
    char tiny[16];
    TEST_ASSERT_FALSE(vmav_status_ok(
        vmav_encode_state_default_cache_dir("/very/long/path/to/input.mkv", tiny, sizeof(tiny))));
}

/* ---- fingerprint ---- */

static void test_fingerprint_matches_stat(void) {
    char wd[256];
    TEST_ASSERT_EQUAL_INT(0, make_workdir(wd, sizeof(wd)));
    char input[300];
    snprintf(input, sizeof(input), "%s/in.bin", wd);
    write_dummy_input(input, 1024);

    int64_t size = -1;
    int64_t mtime = -1;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_compute_fingerprint(input, &size, &mtime)));
    TEST_ASSERT_EQUAL_INT64(1024, size);
    TEST_ASSERT_GREATER_THAN_INT64(0, mtime);
}

/* ---- end-to-end load → mutate → save → load round trip ---- */

static void test_save_and_load_round_trip(void) {
    char wd[256];
    TEST_ASSERT_EQUAL_INT(0, make_workdir(wd, sizeof(wd)));
    char input[300];
    snprintf(input, sizeof(input), "%s/in.bin", wd);
    write_dummy_input(input, 2048);
    char cache[300];
    snprintf(cache, sizeof(cache), "%s/cache", wd);

    /* First load: cache empty, fingerprint match false (because the
     * file doesn't exist yet), state initialized fresh. */
    vmav_encode_state_t st;
    bool fp_match = true;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_load(cache, input, &st, &fp_match)));
    TEST_ASSERT_FALSE(fp_match);
    TEST_ASSERT_EQUAL_INT(VMAV_STATE_SCHEMA_VERSION, st.schema_version);
    TEST_ASSERT_EQUAL_INT64(2048, st.input_size);

    /* Populate every field. */
    st.crop = (vmav_state_crop_t){VMAV_STEP_COMPLETE, 4, 8, 2, 6};
    st.grain = (vmav_state_grain_t){VMAV_STEP_COMPLETE, 0.42};
    st.crf = (vmav_state_crf_t){VMAV_STEP_COMPLETE, 31, 92.5, true};

    vmav_state_audio_t a1 = {VMAV_STEP_COMPLETE, 1, "eng", "Surround 7.1", true, ""};
    snprintf(a1.output_path, sizeof(a1.output_path), "%s/a1.opus", wd);
    vmav_state_audio_t a2 = {VMAV_STEP_PENDING, 2, "fre", "VFF", false, ""};
    snprintf(a2.output_path, sizeof(a2.output_path), "%s/a2.opus", wd);
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_add_audio(&st, &a1)));
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_add_audio(&st, &a2)));

    vmav_state_sub_t s1 = {VMAV_STEP_COMPLETE, 5, "eng", false, false, "", 89};
    snprintf(s1.output_path, sizeof(s1.output_path), "%s/s1.srt", wd);
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_add_sub(&st, &s1)));

    st.video = (vmav_state_video_t){VMAV_STEP_FAILED, "", 0};
    snprintf(st.video.output_path, sizeof(st.video.output_path), "%s/out.ivf", wd);
    st.video.frame_count = 7219;
    st.mux = (vmav_state_mux_t){VMAV_STEP_PENDING, ""};

    /* Save. */
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_save(cache, &st)));
    vmav_encode_state_free(&st);

    /* Load back: fingerprint matches (input unchanged), every field
     * round-trips byte-for-byte where strings are concerned and
     * exact-equal where numbers are. */
    vmav_encode_state_t st2;
    fp_match = false;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_load(cache, input, &st2, &fp_match)));
    TEST_ASSERT_TRUE(fp_match);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, st2.crop.status);
    TEST_ASSERT_EQUAL_INT(4, st2.crop.top);
    TEST_ASSERT_EQUAL_INT(8, st2.crop.bottom);
    TEST_ASSERT_EQUAL_INT(2, st2.crop.left);
    TEST_ASSERT_EQUAL_INT(6, st2.crop.right);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, st2.grain.status);
    TEST_ASSERT_EQUAL_DOUBLE(0.42, st2.grain.score);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, st2.crf.status);
    TEST_ASSERT_EQUAL_INT(31, st2.crf.crf);
    TEST_ASSERT_EQUAL_DOUBLE(92.5, st2.crf.vmaf);
    TEST_ASSERT_TRUE(st2.crf.escalated);

    TEST_ASSERT_EQUAL_UINT64(2u, st2.audio_count);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_COMPLETE, st2.audio[0].status);
    TEST_ASSERT_EQUAL_INT(1, st2.audio[0].stream_index);
    TEST_ASSERT_EQUAL_STRING("eng", st2.audio[0].language);
    TEST_ASSERT_EQUAL_STRING("Surround 7.1", st2.audio[0].title);
    TEST_ASSERT_TRUE(st2.audio[0].is_default);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_PENDING, st2.audio[1].status);
    TEST_ASSERT_EQUAL_STRING("fre", st2.audio[1].language);

    TEST_ASSERT_EQUAL_UINT64(1u, st2.sub_count);
    TEST_ASSERT_EQUAL_INT(89, st2.subs[0].subtitle_count);
    TEST_ASSERT_EQUAL_STRING("eng", st2.subs[0].language);

    TEST_ASSERT_EQUAL_INT(VMAV_STEP_FAILED, st2.video.status);
    TEST_ASSERT_EQUAL_INT64(7219, st2.video.frame_count);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_PENDING, st2.mux.status);

    vmav_encode_state_free(&st2);
}

/* ---- fingerprint mismatch invalidates state ---- */

static void test_fingerprint_mismatch_invalidates(void) {
    char wd[256];
    TEST_ASSERT_EQUAL_INT(0, make_workdir(wd, sizeof(wd)));
    char input[300];
    snprintf(input, sizeof(input), "%s/in.bin", wd);
    write_dummy_input(input, 1024);
    char cache[300];
    snprintf(cache, sizeof(cache), "%s/cache", wd);

    /* Save a state for the 1024-byte file. */
    vmav_encode_state_t st;
    bool fp_match;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_load(cache, input, &st, &fp_match)));
    st.crf.status = VMAV_STEP_COMPLETE;
    st.crf.crf = 28;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_save(cache, &st)));
    vmav_encode_state_free(&st);

    /* Rewrite the input at a different size — fingerprint must
     * invalidate, loaded state must be fresh, not the saved CRF. */
    write_dummy_input(input, 4096);

    vmav_encode_state_t st2;
    fp_match = true;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_encode_state_load(cache, input, &st2, &fp_match)));
    TEST_ASSERT_FALSE(fp_match);
    TEST_ASSERT_EQUAL_INT(VMAV_STEP_PENDING, st2.crf.status);
    TEST_ASSERT_EQUAL_INT(0, st2.crf.crf);
    TEST_ASSERT_EQUAL_INT64(4096, st2.input_size);
    vmav_encode_state_free(&st2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_status_strings_round_trip);
    RUN_TEST(test_default_cache_dir);
    RUN_TEST(test_fingerprint_matches_stat);
    RUN_TEST(test_save_and_load_round_trip);
    RUN_TEST(test_fingerprint_mismatch_invalidates);
    return UNITY_END();
}
