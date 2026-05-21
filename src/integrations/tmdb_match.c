#include "vmavificient/vmav_tmdb.h"

#include "cJSON.h"
#include "util/json_io.h"

#include <stdio.h>
#include <string.h>

vmav_status_t
vmav_tmdb_parse_movie_response(const char *json_body, int tmdb_id, vmav_tmdb_movie_t *out) {
    if (json_body == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_tmdb_parse_movie_response: null arg");
    }
    memset(out, 0, sizeof(*out));
    out->tmdb_id = tmdb_id;

    cJSON *root = cJSON_Parse(json_body);
    if (root == NULL) {
        return VMAV_ERR(VMAV_ERR_PARSE, "TMDB response is not valid JSON");
    }

    /* If the response has `success: false` it's an API error envelope. */
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "success");
    if (ok != NULL && cJSON_IsFalse(ok)) {
        const char *msg = vmav_json_get_string(root, "status_message", "?");
        char msg_copy[128];
        snprintf(msg_copy, sizeof(msg_copy), "%s", msg);
        cJSON_Delete(root);
        return VMAV_ERR(VMAV_ERR_NOT_FOUND, "TMDB error: %s", msg_copy);
    }

    const char *title = vmav_json_get_string(root, "original_title", "");
    snprintf(out->original_title, sizeof(out->original_title), "%s", title);

    const char *lang = vmav_json_get_string(root, "original_language", "");
    snprintf(out->original_language, sizeof(out->original_language), "%s", lang);

    const char *date = vmav_json_get_string(root, "release_date", "");
    int y = 0;
    if (date[0] != '\0') {
        (void)sscanf(date, "%d-", &y);
    }
    out->release_year = y;

    cJSON_Delete(root);

    if (out->original_title[0] == '\0' || out->release_year <= 0 ||
        out->original_language[0] == '\0') {
        return VMAV_ERR(VMAV_ERR_PARSE,
                        "TMDB response missing required fields "
                        "(title='%s', year=%d, lang='%s')",
                        out->original_title,
                        out->release_year,
                        out->original_language);
    }
    return VMAV_OK_STATUS;
}
