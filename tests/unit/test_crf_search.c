/* Tests for the crf_search module's pure-math API.
 * Production `vmav_crf_search` needs a real input file — that goes
 * into m6's integration tests once we have a fixture in tree. */

#include "vmavificient/vmav_crf_search.h"

#include "unity.h"

#include <math.h>
#include <stdbool.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ---- pick_samples ---- */

static void test_pick_samples_single_picks_middle(void) {
    int64_t starts[1];
    const int n = vmav_crf_pick_samples(/*total=*/3000, 1, /*frames_per_sample=*/480, starts);
    TEST_ASSERT_EQUAL_INT(1, n);
    /* margin = 300, lo = 300, hi = 3000 - 300 - 480 = 2220. mid = 1260. */
    TEST_ASSERT_EQUAL_INT64(1260, starts[0]);
}

static void test_pick_samples_multiple_spaced(void) {
    int64_t starts[3];
    const int n = vmav_crf_pick_samples(10000, 3, 480, starts);
    TEST_ASSERT_EQUAL_INT(3, n);
    /* margin = 1000, lo = 1000, hi = 10000 - 1000 - 480 = 8520. step = 3760. */
    TEST_ASSERT_EQUAL_INT64(1000, starts[0]);
    TEST_ASSERT_EQUAL_INT64(4760, starts[1]);
    TEST_ASSERT_EQUAL_INT64(8520, starts[2]);
}

static void test_pick_samples_short_input_clamps_to_one(void) {
    int64_t starts[3];
    const int n = vmav_crf_pick_samples(500, 3, 480, starts);
    /* Short input falls into the lo>=hi branch; returns 1 sample. */
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT64(10, starts[0]);
}

/* ---- next_guess ---- */

static void test_next_guess_slope_interpolates(void) {
    /* CRF 25 → VMAF 96, CRF 30 → VMAF 93. Target 95.
     * slope = (93-96)/(30-25) = -0.6.
     * next = 30 + (95-93)/(-0.6) = 30 - 3.33 = 26.67 → 27 */
    const int g = vmav_crf_next_guess(25, 96.0, 30, 93.0, 95, 18, 50);
    TEST_ASSERT_EQUAL_INT(27, g);
}

static void test_next_guess_clamps_to_range(void) {
    /* Steep slope predicting CRF above the clamp ceiling. */
    const int g = vmav_crf_next_guess(20, 100.0, 22, 99.0, 50, 18, 30);
    TEST_ASSERT_LESS_OR_EQUAL_INT(30, g);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(18, g);
}

static void test_next_guess_falls_back_default_slope_on_flat(void) {
    /* Same CRF in both history points → no real slope → falls back to
     * the default constant slope. */
    const int g = vmav_crf_next_guess(30, 93.0, 30, 93.0, 95, 18, 50);
    /* Default slope is -0.6, target above current → predicts lower CRF. */
    TEST_ASSERT_LESS_THAN_INT(30, g);
}

/* ---- binary search with mock scorer ---- */

typedef struct {
    int calls;
    /* Mock score function: VMAF = 100 - (crf - 18) * 0.6, capped at [60, 100]. */
} mock_ctx_t;

static vmav_status_t mock_score(int crf, void *ud, double *out_vmaf) {
    mock_ctx_t *ctx = ud;
    ctx->calls++;
    double v = 100.0 - (double)(crf - 18) * 0.6;
    if (v < 60.0) {
        v = 60.0;
    }
    if (v > 100.0) {
        v = 100.0;
    }
    *out_vmaf = v;
    return VMAV_OK_STATUS;
}

static void test_binary_search_converges_to_target(void) {
    /* Mock: VMAF = 100 - 0.6*(crf - 18). Target = 92.
     * 92 = 100 - 0.6*(crf-18) → crf = 18 + (100-92)/0.6 = 18 + 13.33 = 31.33 → 31. */
    mock_ctx_t ctx = {0};
    int crf_out = -1;
    double vmaf_out = 0.0;
    vmav_status_t st =
        vmav_crf_binary_search(92, 18, 50, 30, mock_score, &ctx, &crf_out, &vmaf_out);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    /* Result should be within 1 CRF of the analytic answer. */
    TEST_ASSERT_TRUE(crf_out >= 30 && crf_out <= 32);
    /* And VMAF should be within the convergence band of the target. */
    TEST_ASSERT_TRUE(fabs(vmaf_out - 92.0) <= 1.0);
    /* Should have used very few trials given linear mock. */
    TEST_ASSERT_LESS_OR_EQUAL_INT(VMAV_CRF_MAX_TRIALS, ctx.calls);
}

static void test_binary_search_respects_clamp(void) {
    mock_ctx_t ctx = {0};
    int crf_out = -1;
    double vmaf_out = 0.0;
    /* Target unreachable (VMAF=99 needs CRF<19); clamp at 25. */
    vmav_status_t st =
        vmav_crf_binary_search(99, 25, 50, 30, mock_score, &ctx, &crf_out, &vmaf_out);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(25, crf_out);
    TEST_ASSERT_LESS_OR_EQUAL_INT(50, crf_out);
}

static vmav_status_t mock_score_failing(int crf, void *ud, double *out_vmaf) {
    (void)crf;
    int *calls = ud;
    (*calls)++;
    if (*calls == 1) {
        *out_vmaf = 92.0;
        return VMAV_OK_STATUS;
    }
    return VMAV_ERR(VMAV_ERR_FFMPEG, "mock encode failure");
}

static void test_binary_search_propagates_scorer_failure(void) {
    int calls = 0;
    int crf_out = -1;
    double vmaf_out = 0.0;
    /* Target far enough off that 1 call won't converge → 2nd call fails. */
    vmav_status_t st =
        vmav_crf_binary_search(85, 18, 50, 30, mock_score_failing, &calls, &crf_out, &vmaf_out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_FFMPEG, st.code);
}

static void test_binary_search_rejects_bad_args(void) {
    int crf_out = -1;
    double vmaf_out = 0.0;
    /* null fn. */
    vmav_status_t st = vmav_crf_binary_search(92, 18, 50, 30, NULL, NULL, &crf_out, &vmaf_out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    /* init outside bounds. */
    mock_ctx_t ctx = {0};
    st = vmav_crf_binary_search(92, 30, 50, 25, mock_score, &ctx, &crf_out, &vmaf_out);
    TEST_ASSERT_FALSE(vmav_status_ok(st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pick_samples_single_picks_middle);
    RUN_TEST(test_pick_samples_multiple_spaced);
    RUN_TEST(test_pick_samples_short_input_clamps_to_one);
    RUN_TEST(test_next_guess_slope_interpolates);
    RUN_TEST(test_next_guess_clamps_to_range);
    RUN_TEST(test_next_guess_falls_back_default_slope_on_flat);
    RUN_TEST(test_binary_search_converges_to_target);
    RUN_TEST(test_binary_search_respects_clamp);
    RUN_TEST(test_binary_search_propagates_scorer_failure);
    RUN_TEST(test_binary_search_rejects_bad_args);
    return UNITY_END();
}
