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

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
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

/**
 * @brief GET a TMDB v3 endpoint and parse the JSON body.
 *
 * @param path  Endpoint path under /3/, without leading slash and without
 *              the api_key parameter (appended here), e.g. "movie/603".
 * @return Parsed document (caller frees with cJSON_Delete) or NULL.
 */
static cJSON *tmdb_get_json(const char *path) {
  const VmavConfig *cfg = config_get();
  if (!cfg->tmdb_api_key[0]) {
    (void)fprintf(stderr, "Error: TMDB API key not found.\n"
                          "Set tmdb_api_key in config.ini (cwd or "
                          "$HOME/.config/vmavificient/config.ini).\n");
    return NULL;
  }

  char url[512];
  snprintf(url, sizeof(url), "https://api.themoviedb.org/3/%s?api_key=%s", path, cfg->tmdb_api_key);

  CURL *curl = curl_easy_init();
  if (!curl) {
    (void)fprintf(stderr, "Error: failed to initialize libcurl.\n");
    return NULL;
  }

  ResponseBuf buf = {.data = NULL, .size = 0};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    (void)fprintf(stderr, "Error: TMDB request failed: %s\n", curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    free(buf.data);
    return NULL;
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  if (http_code != 200) {
    (void)fprintf(stderr, "Error: TMDB returned HTTP %ld for %s.\n", http_code, path);
    free(buf.data);
    return NULL;
  }

  cJSON *json = cJSON_Parse(buf.data);
  free(buf.data);
  if (!json)
    (void)fprintf(stderr, "Error: failed to parse TMDB response.\n");
  return json;
}

/** @brief Copy a string field out of a JSON object, if present. */
static void copy_json_string(const cJSON *json, const char *key, char *out, size_t outsize) {
  const cJSON *item = cJSON_GetObjectItem(json, key);
  if (cJSON_IsString(item))
    snprintf(out, outsize, "%s", item->valuestring);
}

/** @brief Extract the year from a "YYYY-MM-DD" date field, or 0. */
static int json_date_year(const cJSON *json, const char *key) {
  const cJSON *item = cJSON_GetObjectItem(json, key);
  int year = 0;
  if (cJSON_IsString(item)) {
    char *end = NULL;
    long parsed = strtol(item->valuestring, &end, 10);
    if (end != item->valuestring)
      year = (int)parsed;
  }
  return year;
}

TmdbMovieInfo tmdb_fetch_movie(int tmdb_id) {
  TmdbMovieInfo info = {.error = -1, .release_year = 0};
  info.original_title[0] = '\0';
  info.original_language[0] = '\0';

  char path[64];
  snprintf(path, sizeof(path), "movie/%d", tmdb_id);
  cJSON *json = tmdb_get_json(path);
  if (!json)
    return info;

  copy_json_string(json, "original_title", info.original_title, sizeof(info.original_title));
  info.release_year = json_date_year(json, "release_date");
  copy_json_string(json, "original_language", info.original_language,
                   sizeof(info.original_language));
  cJSON_Delete(json);

  if (info.original_title[0] && info.release_year > 0 && info.original_language[0])
    info.error = 0;
  else
    (void)fprintf(stderr, "Error: TMDB response missing required fields.\n");

  return info;
}

TmdbTvInfo tmdb_fetch_tv(int tmdb_id) {
  TmdbTvInfo info = {.error = -1, .first_air_year = 0};
  info.original_name[0] = '\0';
  info.original_language[0] = '\0';

  char path[64];
  snprintf(path, sizeof(path), "tv/%d", tmdb_id);
  cJSON *json = tmdb_get_json(path);
  if (!json)
    return info;

  copy_json_string(json, "original_name", info.original_name, sizeof(info.original_name));
  info.first_air_year = json_date_year(json, "first_air_date");
  copy_json_string(json, "original_language", info.original_language,
                   sizeof(info.original_language));
  cJSON_Delete(json);

  /* first_air_year is informational for TV (not in the filename); only
     the name and language are hard requirements. */
  if (info.original_name[0] && info.original_language[0])
    info.error = 0;
  else
    (void)fprintf(stderr, "Error: TMDB response missing required fields.\n");

  return info;
}

TmdbEpisodeInfo tmdb_fetch_episode(int tmdb_id, int season, int episode) {
  TmdbEpisodeInfo info = {.error = -1};
  info.name[0] = '\0';

  char path[96];
  snprintf(path, sizeof(path), "tv/%d/season/%d/episode/%d", tmdb_id, season, episode);
  cJSON *json = tmdb_get_json(path);
  if (!json)
    return info;

  copy_json_string(json, "name", info.name, sizeof(info.name));
  cJSON_Delete(json);

  if (info.name[0])
    info.error = 0;
  return info;
}
