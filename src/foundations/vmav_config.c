/* vmav_config — INI-backed user config (TMDB API key + release group).
 *
 * The parser is intentionally minimal: line-based, no escaping, no
 * inline comments. Format:
 *
 *   # full-line comment
 *   [section]
 *   key = value
 *
 * Sections recognized: [tmdb], [naming]. Keys recognized: tmdb.api_key,
 * naming.release_group. Anything else is silently skipped — forward
 * compat for future additions. */

#include "vmavificient/vmav_config.h"

#include "vmavificient/vmav_os.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

void vmav_config_init(vmav_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
}

vmav_status_t vmav_config_path(char *buf, size_t bufsize) {
    if (buf == NULL || bufsize == 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "config_path: null arg");
    }
    char dir[VMAV_PATH_MAX];
    const vmav_status_t st = vmav_path_config_dir(dir, sizeof(dir));
    if (!vmav_status_ok(st)) {
        return st;
    }
    return vmav_path_join(buf, bufsize, dir, "config.ini");
}

/* Trim leading + trailing ASCII whitespace in place. */
static char *trim(char *s) {
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

/* Assign a key=value pair into the right cfg field based on the
 * current section. Unknown keys are silently ignored. */
static void assign(vmav_config_t *cfg, const char *section, const char *key, const char *value) {
    if (strcmp(section, "tmdb") == 0 && strcmp(key, "api_key") == 0) {
        snprintf(cfg->tmdb_api_key, sizeof(cfg->tmdb_api_key), "%s", value);
        return;
    }
    if (strcmp(section, "naming") == 0 && strcmp(key, "release_group") == 0) {
        snprintf(cfg->release_group, sizeof(cfg->release_group), "%s", value);
        return;
    }
    /* Unknown — forward compat. */
}

vmav_status_t vmav_config_load(vmav_config_t *out) {
    if (out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "config_load: null arg");
    }
    vmav_config_init(out);

    char path[VMAV_PATH_MAX];
    const vmav_status_t pst = vmav_config_path(path, sizeof(path));
    if (!vmav_status_ok(pst)) {
        return pst;
    }
    if (!vmav_fs_exists(path)) {
        /* No config yet — first run. Empty cfg is the right answer. */
        return VMAV_OK_STATUS;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return VMAV_ERR(VMAV_ERR_IO, "open '%s'", path);
    }

    char section[64] = {0};
    char line[512];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#' || *p == ';') {
            continue;
        }
        if (*p == '[') {
            char *close = strchr(p, ']');
            if (close == NULL) {
                continue;
            }
            *close = '\0';
            snprintf(section, sizeof(section), "%s", trim(p + 1));
            continue;
        }
        char *eq = strchr(p, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        const char *key = trim(p);
        const char *value = trim(eq + 1);
        assign(out, section, key, value);
    }
    fclose(f);
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_config_save(const vmav_config_t *cfg) {
    if (cfg == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "config_save: null arg");
    }
    char path[VMAV_PATH_MAX];
    const vmav_status_t pst = vmav_config_path(path, sizeof(path));
    if (!vmav_status_ok(pst)) {
        return pst;
    }

    /* Ensure the parent dir exists. */
    char dir[VMAV_PATH_MAX];
    const vmav_status_t dst = vmav_path_config_dir(dir, sizeof(dir));
    if (!vmav_status_ok(dst)) {
        return dst;
    }
    const vmav_status_t mkdir_st = vmav_fs_mkdir_p(dir);
    if (!vmav_status_ok(mkdir_st)) {
        return mkdir_st;
    }

    /* Atomic write: <path>.tmp + rename. */
    char tmp_path[VMAV_PATH_MAX];
    const int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "config_save: tmp path overflow");
    }
    FILE *f = fopen(tmp_path, "w");
    if (f == NULL) {
        return VMAV_ERR(VMAV_ERR_IO, "open '%s'", tmp_path);
    }
    fputs("# vmavificient user config\n", f);
    fputs("# https://github.com/matherose/VMAVIFICIENT\n", f);
    fputs("\n", f);
    fputs("[tmdb]\n", f);
    fprintf(f, "api_key = %s\n", cfg->tmdb_api_key);
    fputs("\n", f);
    fputs("[naming]\n", f);
    fprintf(f, "release_group = %s\n", cfg->release_group);
    if (fflush(f) != 0) {
        fclose(f);
        remove(tmp_path);
        return VMAV_ERR(VMAV_ERR_IO, "fflush '%s'", tmp_path);
    }
    fclose(f);

    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return VMAV_ERR(VMAV_ERR_IO, "rename '%s' -> '%s'", tmp_path, path);
    }
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_config_resolve_api_key(char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "resolve_api_key: null arg");
    }
    out[0] = '\0';

    const char *env = getenv("TMDB_API_KEY");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s", env);
        return VMAV_OK_STATUS;
    }

    vmav_config_t cfg;
    const vmav_status_t st = vmav_config_load(&cfg);
    if (!vmav_status_ok(st)) {
        return st;
    }
    if (cfg.tmdb_api_key[0] == '\0') {
        return VMAV_ERR(VMAV_ERR_NOT_FOUND,
                        "no TMDB API key: set $TMDB_API_KEY or run "
                        "`vmavificient encode --config`");
    }
    snprintf(out, out_size, "%s", cfg.tmdb_api_key);
    return VMAV_OK_STATUS;
}
