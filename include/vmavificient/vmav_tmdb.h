#pragma once

#include "vmavificient/vmav_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Movie metadata returned by TMDB's /3/movie/{id} endpoint. All
 * fields are populated on a successful fetch; on partial failure,
 * vmav_tmdb_fetch_movie returns an error and the caller should not
 * trust any field. */
typedef struct {
    int tmdb_id;
    char original_title[512];  /* UTF-8 */
    int release_year;          /* extracted from YYYY-MM-DD release_date */
    char original_language[8]; /* ISO 639-1 (e.g. "en", "fr") */
} vmav_tmdb_movie_t;

/* Fetch movie metadata by TMDB ID. Requires a v3 API key (TMDB's "Bearer"
 * style isn't supported — use the legacy api_key= query string). The
 * call hits https://api.themoviedb.org/3/movie/<id>?api_key=<key>.
 *
 *   tmdb_id     The TMDB integer ID.
 *   api_key     Non-NULL, non-empty v3 API key. Caller-provided so we
 *               never pull from env in library code (testability).
 *   timeout_ms  Total request timeout. 0 = use a 10s default.
 *   out         Filled on success.
 *
 * Returns VMAV_ERR_BAD_ARG / _IO / _SUBPROC / _PARSE / _NOT_FOUND on
 * the obvious failures. */
vmav_status_t vmav_tmdb_fetch_movie(int tmdb_id,
                                    const char *api_key,
                                    uint32_t timeout_ms,
                                    vmav_tmdb_movie_t *out);

/* Pure-parse path: given a /3/movie/<id> JSON response body, populate
 * the struct. Used by unit tests (no network), and by the fetch
 * function itself after the HTTP body arrives. `tmdb_id` is passed
 * through so the caller can correlate the response with the request.
 *
 * Returns VMAV_ERR_PARSE if the body isn't well-formed JSON or
 * VMAV_ERR_BAD_ARG if any required field (original_title, release_date,
 * original_language) is missing. */
vmav_status_t
vmav_tmdb_parse_movie_response(const char *json_body, int tmdb_id, vmav_tmdb_movie_t *out);

#ifdef __cplusplus
}
#endif
