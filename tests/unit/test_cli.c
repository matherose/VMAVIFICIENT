#include "vmavificient/vmav_cli.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
}
void tearDown(void) {
}

static int g_called_cmd = 0;
static int g_called_default = 0;
static int g_called_help = 0;

static int fake_named(int argc, char **argv) {
    (void)argc;
    (void)argv;
    g_called_cmd = 1;
    return 7;
}

static int fake_default(int argc, char **argv) {
    (void)argc;
    (void)argv;
    g_called_default = 1;
    return 11;
}

static int fake_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    g_called_help = 1;
    return 0;
}

static const vmav_subcmd_t TABLE[] = {
    {"foo", "fake foo", fake_named},
    {"encode", "fake default", fake_default},
    {"help", "fake help", fake_help},
    {NULL, NULL, NULL},
};

static void reset_called(void) {
    g_called_cmd = 0;
    g_called_default = 0;
    g_called_help = 0;
}

static void test_find_returns_matching_entry(void) {
    const vmav_subcmd_t *c = vmav_cli_find(TABLE, "foo");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("foo", c->name);
}

static void test_find_returns_null_for_unknown(void) {
    TEST_ASSERT_NULL(vmav_cli_find(TABLE, "nope"));
}

static void test_dispatch_known_subcommand(void) {
    reset_called();
    char *argv[] = {(char *)"vmav", (char *)"foo", NULL};
    int rc = vmav_cli_dispatch(2, argv, TABLE, "encode");
    TEST_ASSERT_EQUAL_INT(7, rc);
    TEST_ASSERT_EQUAL_INT(1, g_called_cmd);
}

static void test_dispatch_no_args_runs_help(void) {
    reset_called();
    char *argv[] = {(char *)"vmav", NULL};
    int rc = vmav_cli_dispatch(1, argv, TABLE, "encode");
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, g_called_help);
}

static void test_dispatch_double_dash_help(void) {
    reset_called();
    char *argv[] = {(char *)"vmav", (char *)"--help", NULL};
    int rc = vmav_cli_dispatch(2, argv, TABLE, "encode");
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, g_called_help);
}

static void test_dispatch_backward_compat_to_default(void) {
    reset_called();
    char *argv[] = {(char *)"vmav", (char *)"movie.mkv", NULL};
    int rc = vmav_cli_dispatch(2, argv, TABLE, "encode");
    TEST_ASSERT_EQUAL_INT(11, rc);
    TEST_ASSERT_EQUAL_INT(1, g_called_default);
}

static void test_dispatch_unknown_flag_returns_1(void) {
    reset_called();
    char *argv[] = {(char *)"vmav", (char *)"--bogus", NULL};
    int rc = vmav_cli_dispatch(2, argv, TABLE, "encode");
    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_INT(0, g_called_cmd);
    TEST_ASSERT_EQUAL_INT(0, g_called_default);
}

static void test_render_help_includes_all_subcommands(void) {
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/vmav-cli-test-%d.txt", (int)getpid());
    FILE *fp = fopen(tmp_path, "w+");
    TEST_ASSERT_NOT_NULL(fp);
    vmav_cli_render_help(TABLE, fp);
    fflush(fp);
    rewind(fp);
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    (void)remove(tmp_path);
    TEST_ASSERT_NOT_NULL(strstr(buf, "foo"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "encode"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "help"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_find_returns_matching_entry);
    RUN_TEST(test_find_returns_null_for_unknown);
    RUN_TEST(test_dispatch_known_subcommand);
    RUN_TEST(test_dispatch_no_args_runs_help);
    RUN_TEST(test_dispatch_double_dash_help);
    RUN_TEST(test_dispatch_backward_compat_to_default);
    RUN_TEST(test_dispatch_unknown_flag_returns_1);
    RUN_TEST(test_render_help_includes_all_subcommands);
    return UNITY_END();
}
