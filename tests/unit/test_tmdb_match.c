/* Unit tests for vmav_tmdb_parse_movie_response (no network). */

#include "vmavificient/vmav_tmdb.h"

#include "unity.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_happy_path(void) {
    /* Real /3/movie/27205 (Inception) shape, abbreviated. */
    static const char body[] = "{"
                               "\"adult\":false,"
                               "\"backdrop_path\":\"/foo.jpg\","
                               "\"id\":27205,"
                               "\"original_title\":\"Inception\","
                               "\"original_language\":\"en\","
                               "\"release_date\":\"2010-07-15\","
                               "\"runtime\":148,"
                               "\"title\":\"Inception\""
                               "}";
    vmav_tmdb_movie_t m;
    vmav_status_t st = vmav_tmdb_parse_movie_response(body, 27205, &m);
    TEST_ASSERT_TRUE_MESSAGE(vmav_status_ok(st), st.msg);
    TEST_ASSERT_EQUAL_INT(27205, m.tmdb_id);
    TEST_ASSERT_EQUAL_STRING("Inception", m.original_title);
    TEST_ASSERT_EQUAL_INT(2010, m.release_year);
    TEST_ASSERT_EQUAL_STRING("en", m.original_language);
}

static void test_french_film(void) {
    static const char body[] = "{"
                               "\"original_title\":\"Amélie\","
                               "\"original_language\":\"fr\","
                               "\"release_date\":\"2001-04-25\""
                               "}";
    vmav_tmdb_movie_t m;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_tmdb_parse_movie_response(body, 194, &m)));
    /* UTF-8 é must round-trip intact. */
    TEST_ASSERT_EQUAL_STRING("Amélie", m.original_title);
    TEST_ASSERT_EQUAL_STRING("fr", m.original_language);
    TEST_ASSERT_EQUAL_INT(2001, m.release_year);
}

static void test_missing_release_date(void) {
    static const char body[] = "{\"original_title\":\"X\",\"original_language\":\"en\"}";
    vmav_tmdb_movie_t m;
    vmav_status_t st = vmav_tmdb_parse_movie_response(body, 1, &m);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_PARSE, st.code);
}

static void test_missing_title(void) {
    static const char body[] = "{\"original_language\":\"en\",\"release_date\":\"2020-01-01\"}";
    vmav_tmdb_movie_t m;
    vmav_status_t st = vmav_tmdb_parse_movie_response(body, 1, &m);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_PARSE, st.code);
}

static void test_invalid_json_errors(void) {
    vmav_tmdb_movie_t m;
    vmav_status_t st = vmav_tmdb_parse_movie_response("{not json", 1, &m);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_PARSE, st.code);
}

static void test_tmdb_error_envelope(void) {
    /* TMDB's documented error shape. */
    static const char body[] =
        "{\"success\":false,\"status_code\":34,"
        "\"status_message\":\"The resource you requested could not be found.\"}";
    vmav_tmdb_movie_t m;
    vmav_status_t st = vmav_tmdb_parse_movie_response(body, 999999999, &m);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_NOT_FOUND, st.code);
    TEST_ASSERT_NOT_NULL(strstr(st.msg, "could not be found"));
}

static void test_null_arg_errors(void) {
    vmav_tmdb_movie_t m;
    vmav_status_t st = vmav_tmdb_parse_movie_response(NULL, 1, &m);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
    st = vmav_tmdb_parse_movie_response("{}", 1, NULL);
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_BAD_ARG, st.code);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_happy_path);
    RUN_TEST(test_french_film);
    RUN_TEST(test_missing_release_date);
    RUN_TEST(test_missing_title);
    RUN_TEST(test_invalid_json_errors);
    RUN_TEST(test_tmdb_error_envelope);
    RUN_TEST(test_null_arg_errors);
    return UNITY_END();
}
