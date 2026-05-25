/* Unit tests for vmav_config — INI load + save round trip,
 * unknown keys gracefully ignored, env-var precedence for the
 * api-key resolver. */

#include "vmavificient/vmav_config.h"

#include "unity.h"

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

static char g_workdir[1024];

void setUp(void) {
    /* Redirect the config dir to a fresh workdir so we don't trample
     * the user's real ~/.config/vmavificient. The config module's
     * resolver uses XDG_CONFIG_HOME on Linux/macOS — we just set
     * that env var. */
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
        snprintf(
            g_workdir, sizeof(g_workdir), "%s/vmav_config_test_%u_%d", base, (unsigned)getpid(), i);
#ifdef _WIN32
        if (_mkdir(g_workdir) == 0) {
            break;
        }
#else
        if (mkdir(g_workdir, 0700) == 0) {
            break;
        }
#endif
    }
    /* vmav_path_config_dir on POSIX consults XDG_CONFIG_HOME first;
     * on macOS it falls back to $HOME/Library/Application Support.
     * We isolate by setting XDG_CONFIG_HOME on Linux, HOME on macOS. */
#ifdef __APPLE__
    setenv("HOME", g_workdir, 1);
#else
    setenv("XDG_CONFIG_HOME", g_workdir, 1);
#endif
    /* Clean any env that would influence the resolver. */
    unsetenv("TMDB_API_KEY");
}

void tearDown(void) {
}

static void test_path_under_config_dir(void) {
    char p[1024];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_path(p, sizeof(p))));
    /* Must end with config.ini. */
    const size_t n = strlen(p);
    TEST_ASSERT_TRUE(n > strlen("config.ini"));
    TEST_ASSERT_EQUAL_STRING("config.ini", p + n - strlen("config.ini"));
}

static void test_load_missing_returns_empty(void) {
    vmav_config_t cfg;
    cfg.tmdb_api_key[0] = 'X'; /* poison */
    cfg.release_group[0] = 'Y';
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_load(&cfg)));
    TEST_ASSERT_EQUAL_STRING("", cfg.tmdb_api_key);
    TEST_ASSERT_EQUAL_STRING("", cfg.release_group);
}

static void test_save_then_load_round_trip(void) {
    vmav_config_t cfg;
    vmav_config_init(&cfg);
    snprintf(cfg.tmdb_api_key, sizeof(cfg.tmdb_api_key), "deadbeefdeadbeefdeadbeefdeadbeef");
    snprintf(cfg.release_group, sizeof(cfg.release_group), "TESTGROUP");
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_save(&cfg)));

    vmav_config_t loaded;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_load(&loaded)));
    TEST_ASSERT_EQUAL_STRING("deadbeefdeadbeefdeadbeefdeadbeef", loaded.tmdb_api_key);
    TEST_ASSERT_EQUAL_STRING("TESTGROUP", loaded.release_group);
}

static void test_resolve_api_key_env_wins(void) {
    /* Write a config with one key, then set env to a different one.
     * Env must win. */
    vmav_config_t cfg;
    vmav_config_init(&cfg);
    snprintf(cfg.tmdb_api_key, sizeof(cfg.tmdb_api_key), "from-config");
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_save(&cfg)));

    setenv("TMDB_API_KEY", "from-env", 1);
    char resolved[128];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_resolve_api_key(resolved, sizeof(resolved))));
    TEST_ASSERT_EQUAL_STRING("from-env", resolved);
    unsetenv("TMDB_API_KEY");
}

static void test_resolve_api_key_falls_back_to_config(void) {
    vmav_config_t cfg;
    vmav_config_init(&cfg);
    snprintf(cfg.tmdb_api_key, sizeof(cfg.tmdb_api_key), "from-config");
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_save(&cfg)));

    char resolved[128];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_resolve_api_key(resolved, sizeof(resolved))));
    TEST_ASSERT_EQUAL_STRING("from-config", resolved);
}

static void test_resolve_api_key_missing_returns_not_found(void) {
    /* No env, no config file: NOT_FOUND. */
    char resolved[128];
    const vmav_status_t st = vmav_config_resolve_api_key(resolved, sizeof(resolved));
    TEST_ASSERT_FALSE(vmav_status_ok(st));
    TEST_ASSERT_EQUAL_INT(VMAV_ERR_NOT_FOUND, st.code);
}

static void test_parser_tolerates_blank_and_comment_lines(void) {
    /* Hand-craft an INI on disk via the save path, then prepend
     * blanks/comments/garbage and reload. */
    vmav_config_t cfg;
    vmav_config_init(&cfg);
    snprintf(cfg.tmdb_api_key, sizeof(cfg.tmdb_api_key), "abc");
    snprintf(cfg.release_group, sizeof(cfg.release_group), "GROUP");
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_save(&cfg)));

    char path[1024];
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_path(path, sizeof(path))));

    /* Append noise: blank line, # comment, ; comment, unknown key. */
    FILE *f = fopen(path, "a");
    TEST_ASSERT_NOT_NULL(f);
    fputs("\n", f);
    fputs("# comment\n", f);
    fputs("; semicolon comment\n", f);
    fputs("garbage_no_section_key=value\n", f);
    fputs("[unknown]\n", f);
    fputs("foo = bar\n", f);
    fclose(f);

    vmav_config_t loaded;
    TEST_ASSERT_TRUE(vmav_status_ok(vmav_config_load(&loaded)));
    TEST_ASSERT_EQUAL_STRING("abc", loaded.tmdb_api_key);
    TEST_ASSERT_EQUAL_STRING("GROUP", loaded.release_group);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_path_under_config_dir);
    RUN_TEST(test_load_missing_returns_empty);
    RUN_TEST(test_save_then_load_round_trip);
    RUN_TEST(test_resolve_api_key_env_wins);
    RUN_TEST(test_resolve_api_key_falls_back_to_config);
    RUN_TEST(test_resolve_api_key_missing_returns_not_found);
    RUN_TEST(test_parser_tolerates_blank_and_comment_lines);
    return UNITY_END();
}
