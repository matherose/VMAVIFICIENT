#include "util/str_utils.h"

#include "unity.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {
}
void tearDown(void) {
}

static void test_contains_ci_finds(void) {
    TEST_ASSERT_TRUE(vmav_str_contains_ci("Hello, World!", "world"));
    TEST_ASSERT_TRUE(vmav_str_contains_ci("ABC", "abc"));
    TEST_ASSERT_TRUE(vmav_str_contains_ci("foo bar baz", "BAR"));
    TEST_ASSERT_TRUE(vmav_str_contains_ci("anything", ""));
}

static void test_contains_ci_misses(void) {
    TEST_ASSERT_FALSE(vmav_str_contains_ci("Hello", "world"));
    TEST_ASSERT_FALSE(vmav_str_contains_ci("short", "longer"));
    TEST_ASSERT_FALSE(vmav_str_contains_ci(NULL, "x"));
    TEST_ASSERT_FALSE(vmav_str_contains_ci("x", NULL));
}

static void test_starts_with(void) {
    TEST_ASSERT_TRUE(vmav_str_starts_with("foobar", "foo"));
    TEST_ASSERT_TRUE(vmav_str_starts_with("foo", "foo"));
    TEST_ASSERT_TRUE(vmav_str_starts_with("anything", ""));
    TEST_ASSERT_FALSE(vmav_str_starts_with("foobar", "bar"));
    TEST_ASSERT_FALSE(vmav_str_starts_with("ab", "abc"));
    TEST_ASSERT_FALSE(vmav_str_starts_with(NULL, "x"));
}

static void test_ends_with(void) {
    TEST_ASSERT_TRUE(vmav_str_ends_with("movie.mkv", ".mkv"));
    TEST_ASSERT_TRUE(vmav_str_ends_with("foo", "foo"));
    TEST_ASSERT_TRUE(vmav_str_ends_with("any", ""));
    TEST_ASSERT_FALSE(vmav_str_ends_with("movie.mp4", ".mkv"));
    TEST_ASSERT_FALSE(vmav_str_ends_with("a", "ab"));
}

static void test_dup_round_trip(void) {
    char *p = vmav_str_dup("hello");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("hello", p);
    free(p);
    TEST_ASSERT_NULL(vmav_str_dup(NULL));
}

static void test_to_lower(void) {
    char buf[32];
    vmav_str_to_lower("Hello, World!", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("hello, world!", buf);
    vmav_str_to_lower("ALREADY", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("already", buf);
    /* Truncation: buf too small. */
    char small[4];
    vmav_str_to_lower("ABCDEFG", small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("abc", small);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_contains_ci_finds);
    RUN_TEST(test_contains_ci_misses);
    RUN_TEST(test_starts_with);
    RUN_TEST(test_ends_with);
    RUN_TEST(test_dup_round_trip);
    RUN_TEST(test_to_lower);
    return UNITY_END();
}
