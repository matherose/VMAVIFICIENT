#include "unity.h"
#include "util/pathbuf.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/* === basename ================================================ */

static void test_basename_simple(void) {
    char buf[64];
    vmav_path_basename("/a/b/c.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("c.mkv", buf);
}

static void test_basename_trailing_slash(void) {
    char buf[64];
    vmav_path_basename("/a/b/", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("b", buf);
}

static void test_basename_no_separator(void) {
    char buf[64];
    vmav_path_basename("movie.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("movie.mkv", buf);
}

static void test_basename_windows_separator(void) {
    char buf[64];
    vmav_path_basename("C:\\Users\\foo\\bar.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("bar.mkv", buf);
}

static void test_basename_empty_or_null(void) {
    char buf[64];
    vmav_path_basename("", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
    vmav_path_basename(NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* === dirname ================================================= */

static void test_dirname_simple(void) {
    char buf[64];
    vmav_path_dirname("/a/b/c.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("/a/b", buf);
}

static void test_dirname_no_separator(void) {
    char buf[64];
    vmav_path_dirname("movie.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(".", buf);
}

static void test_dirname_root(void) {
    char buf[64];
    vmav_path_dirname("/movie.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("/", buf);
}

/* === extension ============================================== */

static void test_extension_simple(void) {
    char buf[16];
    vmav_path_extension("movie.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(".mkv", buf);
}

static void test_extension_double_extension(void) {
    char buf[16];
    vmav_path_extension("movie.mp4.bak", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(".bak", buf);
}

static void test_extension_hidden_file(void) {
    char buf[16];
    vmav_path_extension(".bashrc", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_extension_none(void) {
    char buf[16];
    vmav_path_extension("README", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_extension_only_in_basename(void) {
    /* The dot in the parent dir shouldn't fool us. */
    char buf[16];
    vmav_path_extension("/a.b.c/README", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* === stem =================================================== */

static void test_stem_strips_extension(void) {
    char buf[64];
    vmav_path_stem("/a/b/movie.mkv", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("movie", buf);
}

static void test_stem_double_extension(void) {
    char buf[64];
    vmav_path_stem("movie.mp4.bak", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("movie.mp4", buf);
}

static void test_stem_no_extension(void) {
    char buf[64];
    vmav_path_stem("/a/README", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("README", buf);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_basename_simple);
    RUN_TEST(test_basename_trailing_slash);
    RUN_TEST(test_basename_no_separator);
    RUN_TEST(test_basename_windows_separator);
    RUN_TEST(test_basename_empty_or_null);
    RUN_TEST(test_dirname_simple);
    RUN_TEST(test_dirname_no_separator);
    RUN_TEST(test_dirname_root);
    RUN_TEST(test_extension_simple);
    RUN_TEST(test_extension_double_extension);
    RUN_TEST(test_extension_hidden_file);
    RUN_TEST(test_extension_none);
    RUN_TEST(test_extension_only_in_basename);
    RUN_TEST(test_stem_strips_extension);
    RUN_TEST(test_stem_double_extension);
    RUN_TEST(test_stem_no_extension);
    return UNITY_END();
}
