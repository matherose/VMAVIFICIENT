#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_os.h"
#include "vmavificient/vmav_tmdb.h"
#include "vmavificient/vmav_ui.h"

#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_id_arg(int argc, char **argv, int *out_id) {
    *out_id = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            char *end = NULL;
            const long v = strtol(argv[i + 1], &end, 10);
            if (end != NULL && *end == '\0' && v > 0 && v < (long)INT32_MAX) {
                *out_id = (int)v;
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

int cmd_search_run(int argc, char **argv) {
    int tmdb_id = 0;
    if (parse_id_arg(argc, argv, &tmdb_id) != 0) {
        fputs("vmavificient search: expected '--id <TMDB_ID>'.\n"
              "Title-based search lands in a later phase.\n",
              stderr);
        return 2;
    }

    const char *api_key = vmav_env_get("VMAV_TMDB_API_KEY");
    if (api_key == NULL || api_key[0] == '\0') {
        api_key = vmav_env_get("TMDB_API_KEY");
    }
    if (api_key == NULL || api_key[0] == '\0') {
        fputs("vmavificient search: VMAV_TMDB_API_KEY (or TMDB_API_KEY) "
              "env var not set.\n",
              stderr);
        return 2;
    }

    vmav_tmdb_movie_t movie;
    vmav_status_t st = vmav_tmdb_fetch_movie(tmdb_id, api_key, 0, &movie);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient search: %s\n", st.msg);
        return 1;
    }

    vmav_ui_table_t *t = vmav_ui_table_new("TMDB movie");
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%d", movie.tmdb_id);
    char year_str[16];
    snprintf(year_str, sizeof(year_str), "%d", movie.release_year);
    vmav_ui_table_add(t, "id", id_str);
    vmav_ui_table_add(t, "original title", movie.original_title);
    vmav_ui_table_add(t, "release year", year_str);
    vmav_ui_table_add(t, "original language", movie.original_language);
    vmav_ui_table_render(t, stdout);
    vmav_ui_table_free(t);
    return 0;
}
