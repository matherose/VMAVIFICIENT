/**
 * @file config.c
 * @brief INI loader for vmavificient runtime configuration.
 *
 * No defaults: every required key must be present and non-empty in the
 * config file. The program aborts at startup otherwise.
 *
 * Search order:
 *   1. $HOME/.config/vmavificient/config.ini  (primary; written by --config)
 *   2. ./config.ini                            (legacy dev fallback)
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static VmavConfig g_config;
static int g_loaded = 0;

static char *strip(char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1]))
    *--end = '\0';
  return s;
}

static void copy_value(char *dst, size_t dstsize, const char *src) {
  if (dst == NULL || dstsize == 0) {
    if (dst != NULL && dstsize == 0)
      dst[0] = '\0';
    return;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return;
  }

  size_t len = strlen(src);
  if (len >= 2 &&
      ((src[0] == '"' && src[len - 1] == '"') || (src[0] == '\'' && src[len - 1] == '\''))) {
    src++;
    len -= 2;
  }
  if (len >= dstsize)
    len = dstsize - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static int parse_file(const char *path, VmavConfig *cfg) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  char line[512];
  while (fgets(line, sizeof(line), f) != NULL) {
    char *p = strip(line);
    if (*p == '\0' || *p == '#' || *p == ';' || *p == '[')
      continue;

    char *eq = strchr(p, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *key = strip(p);
    char *val = strip(eq + 1);

    if (strcmp(key, "tmdb_api_key") == 0)
      copy_value(cfg->tmdb_api_key, sizeof(cfg->tmdb_api_key), val);
    else if (strcmp(key, "release_group") == 0)
      copy_value(cfg->release_group, sizeof(cfg->release_group), val);
  }

  /* Check for read error (not EOF) */
  if (ferror(f)) {
    fclose(f);
    return -1;
  }

  fclose(f);
  return 0;
}

/* List of required keys + accessors. Add new entries here when introducing
   a new config field. */
typedef struct {
  const char *name;
  size_t offset;
} RequiredKey;

/* Inlined rather than macro-expanded so clang-format produces stable output
   across versions — the stringizing macro confused clang-format-18's
   braced-list handling. */
static const RequiredKey REQUIRED_KEYS[] = {
    {"tmdb_api_key", offsetof(VmavConfig, tmdb_api_key)},
    {"release_group", offsetof(VmavConfig, release_group)},
};

static void die_config(const char *msg, const char *path_tried) {
  fprintf(stderr,
          "Error: %s\n"
          "  Config search order:\n"
          "    1. $HOME/.config/vmavificient/config.ini\n"
          "    2. ./config.ini  (legacy dev fallback)\n"
          "  Run `vmavificient --config` to create the file interactively.\n",
          msg);
  if (path_tried)
    fprintf(stderr, "  Last tried: %s\n", path_tried);
  exit(1);
}

void config_init(void) {
  if (g_loaded)
    return;

  memset(&g_config, 0, sizeof(g_config));

  int found = 0;
  char home_path[1024] = "";

  /* Primary location: written by `--config`. Try this first so installed
     packages (Homebrew, .deb, AUR) Just Work after `vmavificient --config`. */
  const char *home = getenv("HOME");
  if (home) {
    snprintf(home_path, sizeof(home_path), "%s/.config/vmavificient/config.ini", home);
    if (parse_file(home_path, &g_config) == 0)
      found = 1;
  }

  /* Legacy fallback for the dev workflow (running from the build tree
     with config.ini next to the binary). */
  if (!found && parse_file("config.ini", &g_config) == 0)
    found = 1;

  if (!found)
    die_config("No config found.", home_path[0] ? home_path : NULL);

  /* Validate every required key is non-empty. */
  for (size_t i = 0; i < sizeof(REQUIRED_KEYS) / sizeof(REQUIRED_KEYS[0]); i++) {
    const char *val = (const char *)&g_config + REQUIRED_KEYS[i].offset;
    if (val[0] == '\0') {
      char msg[256];
      snprintf(msg, sizeof(msg), "config: required key '%s' is missing or empty.",
               REQUIRED_KEYS[i].name);
      die_config(msg, NULL);
    }
  }

  g_loaded = 1;
}

const VmavConfig *config_get(void) {
  if (!g_loaded)
    config_init();
  return &g_config;
}

/* ---- Interactive setup (--config) ---------------------------------- */

/**
 * @brief Read a line from stdin, strip the trailing newline, into @p buf.
 * @return 0 on success, -1 on EOF or empty input when @p required is non-zero.
 */
static int prompt_line(const char *label, char *buf, size_t bufsize, int required) {
  printf("%s", label);
  fflush(stdout);
  if (!fgets(buf, (int)bufsize, stdin))
    return -1;
  size_t n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
    buf[--n] = '\0';
  if (required && n == 0)
    return -1;
  return 0;
}

/**
 * @brief `mkdir -p` for the parent directories of @p file_path, ignoring
 *        EEXIST.
 * @return 0 on success, -1 on error (errno set by underlying call).
 */
static int mkdir_p_for_file(const char *file_path) {
  /* Walk the path and mkdir each parent. We need this because the
     target ($HOME/.config/vmavificient/config.ini) usually has at least
     one missing directory level on a fresh checkout / brew install. */
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", file_path);
  for (char *p = tmp + 1; *p; p++) {
    if (*p != '/')
      continue;
    *p = '\0';
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
      return -1;
    *p = '/';
  }
  return 0;
}

int config_interactive_setup(void) {
  const char *home = getenv("HOME");
  if (!home || !home[0]) {
    fprintf(stderr, "Error: $HOME is not set; cannot locate config dir.\n");
    return 1;
  }

  char config_path[1024];
  snprintf(config_path, sizeof(config_path), "%s/.config/vmavificient/config.ini", home);

  /* If a config already exists, confirm before overwriting. */
  struct stat st;
  if (stat(config_path, &st) == 0 && st.st_size > 0) {
    printf("A config already exists at %s.\n", config_path);
    char confirm[16];
    if (prompt_line("Overwrite? [y/N]: ", confirm, sizeof(confirm), 0) != 0 ||
        (confirm[0] != 'y' && confirm[0] != 'Y')) {
      printf("Aborted. Existing config left untouched.\n");
      return 0;
    }
  }

  printf("\nvmavificient setup. Two values are required:\n"
         "  - TMDB API key (v3 auth)\n"
         "      get one at https://www.themoviedb.org/settings/api\n"
         "  - Release group tag (e.g. \"matherose\")\n\n");

  char tmdb_api_key[128];
  if (prompt_line("TMDB API key: ", tmdb_api_key, sizeof(tmdb_api_key), 1) != 0) {
    fprintf(stderr, "Error: TMDB API key cannot be empty.\n");
    return 1;
  }

  char release_group[64];
  if (prompt_line("Release group: ", release_group, sizeof(release_group), 1) != 0) {
    fprintf(stderr, "Error: release group cannot be empty.\n");
    return 1;
  }

  if (mkdir_p_for_file(config_path) != 0) {
    fprintf(stderr, "Error: cannot create config directory: %s\n", strerror(errno));
    return 1;
  }

  FILE *f = fopen(config_path, "w");
  if (!f) {
    fprintf(stderr, "Error: cannot open %s for writing: %s\n", config_path, strerror(errno));
    return 1;
  }
  fprintf(f, "# vmavificient config — written by `vmavificient --config`.\n");
  fprintf(f, "tmdb_api_key = %s\n", tmdb_api_key);
  fprintf(f, "release_group = %s\n", release_group);
  fclose(f);

  /* The TMDB key is sensitive; chmod 600 so it isn't world-readable. */
  if (chmod(config_path, S_IRUSR | S_IWUSR) != 0) {
    fprintf(stderr, "Warning: could not set 0600 permissions on %s: %s\n", config_path,
            strerror(errno));
  }

  printf("\nWrote %s\n", config_path);
  return 0;
}
