/**
 * @file tmdb.c
 * @brief Implementation of the TMDB API client.
 */

#include "tmdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <curl/curl.h>

#include "config.h"

/** @brief Buffer for accumulating HTTP response data. */
typedef struct {
  char *data;
  size_t size;
} ResponseBuf;

static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             void *userp) {
  size_t total = size * nmemb;
  ResponseBuf *buf = userp;
  char *tmp = realloc(buf->data, buf->size + total + 1);
  if (!tmp)
    return 0;
  buf->data = tmp;
  memcpy(buf->data + buf->size, contents, total);
  buf->size += total;
  buf->data[buf->size] = '\0';
  return total;
}

TmdbMovieInfo tmdb_fetch_movie(int tmdb_id) {
  TmdbMovieInfo info = {.error = -1, .release_year = 0};
  info.original_title[0] = '\0';
  info.original_language[0] = '\0';

  const VmavConfig *cfg = config_get();
  if (!cfg->tmdb_api_key[0]) {
    fprintf(stderr,
            "Error: TMDB API key not found.\n"
            "Set tmdb_api_key in config.ini (cwd or "
            "$HOME/.config/vmavificient/config.ini).\n");
    return info;
  }

  char url[512];
  snprintf(url, sizeof(url),
           "https://api.themoviedb.org/3/movie/%d?api_key=%s", tmdb_id,
           cfg->tmdb_api_key);

  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Error: failed to initialize libcurl.\n");
    return info;
  }

  ResponseBuf buf = {.data = NULL, .size = 0};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "Error: TMDB request failed: %s\n",
            curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    free(buf.data);
    return info;
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  if (http_code != 200) {
    fprintf(stderr, "Error: TMDB returned HTTP %ld for movie ID %d.\n",
            http_code, tmdb_id);
    free(buf.data);
    return info;
  }

  cJSON *json = cJSON_Parse(buf.data);
  free(buf.data);
  if (!json) {
    fprintf(stderr, "Error: failed to parse TMDB response.\n");
    return info;
  }

  cJSON *title = cJSON_GetObjectItem(json, "original_title");
  if (cJSON_IsString(title))
    snprintf(info.original_title, sizeof(info.original_title), "%s",
             title->valuestring);

  cJSON *date = cJSON_GetObjectItem(json, "release_date");
  if (cJSON_IsString(date))
    sscanf(date->valuestring, "%d-", &info.release_year);

  cJSON *lang = cJSON_GetObjectItem(json, "original_language");
  if (cJSON_IsString(lang))
    snprintf(info.original_language, sizeof(info.original_language), "%s",
             lang->valuestring);

  cJSON_Delete(json);

  if (info.original_title[0] && info.release_year > 0 &&
      info.original_language[0])
    info.error = 0;
  else
    fprintf(stderr, "Error: TMDB response missing required fields.\n");

  return info;
}
