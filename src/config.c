/**
 * @file config.c
 * @brief INI loader for vmavificient runtime configuration.
 *
 * No defaults: every required key must be present and non-empty in
 * config.ini. The program aborts at startup otherwise.
 */

#include "config.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  size_t len = strlen(src);
  if (len >= 2 && ((src[0] == '"' && src[len - 1] == '"') ||
                   (src[0] == '\'' && src[len - 1] == '\''))) {
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
  while (fgets(line, sizeof(line), f)) {
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
  fclose(f);
  return 0;
}

/* List of required keys + accessors. Add new entries here when introducing
   a new config field. */
typedef struct {
  const char *name;
  size_t offset;
} RequiredKey;

#define REQ(field) {#field, offsetof(VmavConfig, field)}
static const RequiredKey REQUIRED_KEYS[] = {
    REQ(tmdb_api_key),
    REQ(release_group),
};
#undef REQ

static void die_config(const char *msg, const char *path_tried) {
  fprintf(stderr,
          "Error: %s\n"
          "  config.ini search order:\n"
          "    1. ./config.ini\n"
          "    2. $HOME/.config/vmavificient/config.ini\n"
          "  Copy config.ini.example to config.ini and fill in the values:\n"
          "    cp config.ini.example config.ini\n",
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

  if (parse_file("config.ini", &g_config) == 0) {
    found = 1;
  } else {
    const char *home = getenv("HOME");
    if (home) {
      snprintf(home_path, sizeof(home_path),
               "%s/.config/vmavificient/config.ini", home);
      if (parse_file(home_path, &g_config) == 0)
        found = 1;
    }
  }

  if (!found)
    die_config("config.ini not found.", home_path[0] ? home_path : NULL);

  /* Validate every required key is non-empty. */
  for (size_t i = 0; i < sizeof(REQUIRED_KEYS) / sizeof(REQUIRED_KEYS[0]);
       i++) {
    const char *val = (const char *)&g_config + REQUIRED_KEYS[i].offset;
    if (val[0] == '\0') {
      char msg[256];
      snprintf(msg, sizeof(msg),
               "config.ini: required key '%s' is missing or empty.",
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
