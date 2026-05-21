#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_tmdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#define VMAV_TMDB_DEFAULT_TIMEOUT_MS 10000U
#define VMAV_TMDB_RESPONSE_CAP (1U * 1024U * 1024U) /* 1 MiB sanity cap */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} response_buf_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *user) {
    response_buf_t *buf = (response_buf_t *)user;
    const size_t incoming = size * nmemb;
    if (buf->len + incoming + 1 > VMAV_TMDB_RESPONSE_CAP) {
        return 0; /* abort — refuses oversize responses */
    }
    if (buf->len + incoming + 1 > buf->cap) {
        size_t new_cap = buf->cap > 0 ? buf->cap : 4096;
        while (new_cap < buf->len + incoming + 1) {
            new_cap *= 2;
        }
        char *p = realloc(buf->data, new_cap);
        if (p == NULL) {
            return 0;
        }
        buf->data = p;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
}

vmav_status_t vmav_tmdb_fetch_movie(int tmdb_id,
                                    const char *api_key,
                                    uint32_t timeout_ms,
                                    vmav_tmdb_movie_t *out) {
    if (api_key == NULL || api_key[0] == '\0' || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_tmdb_fetch_movie: null/empty arg");
    }
    if (tmdb_id <= 0) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_tmdb_fetch_movie: tmdb_id must be positive");
    }
    if (timeout_ms == 0) {
        timeout_ms = VMAV_TMDB_DEFAULT_TIMEOUT_MS;
    }

    char url[512];
    const int n = snprintf(
        url, sizeof(url), "https://api.themoviedb.org/3/movie/%d?api_key=%s", tmdb_id, api_key);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "tmdb URL truncated");
    }

    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        return VMAV_ERR(VMAV_ERR_GENERIC, "curl_easy_init failed");
    }

    response_buf_t buf;
    memset(&buf, 0, sizeof(buf));
    vmav_status_t st = VMAV_OK_STATUS;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "vmavificient/2.0");

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        st = VMAV_ERR(VMAV_ERR_IO, "tmdb fetch failed: %s", curl_easy_strerror(rc));
        goto cleanup;
    }

    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);

    if (http == 404) {
        st = VMAV_ERR(VMAV_ERR_NOT_FOUND, "TMDB movie id %d not found", tmdb_id);
        goto cleanup;
    }
    if (http == 401) {
        st = VMAV_ERR(VMAV_ERR_PERMISSION, "TMDB rejected API key (HTTP 401) — check the key");
        goto cleanup;
    }
    if (http != 200) {
        st = VMAV_ERR(VMAV_ERR_IO, "TMDB returned HTTP %ld for movie %d", http, tmdb_id);
        goto cleanup;
    }

    if (buf.data == NULL) {
        st = VMAV_ERR(VMAV_ERR_IO, "TMDB returned empty body");
        goto cleanup;
    }

    st = vmav_tmdb_parse_movie_response(buf.data, tmdb_id, out);

cleanup:
    curl_easy_cleanup(curl);
    free(buf.data);
    return st;
}
