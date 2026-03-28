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

/**
 * @brief Read TMDB API key from .tmdb_api_key file.
 *
 * Searches in current directory, then home directory.
 * Strips trailing whitespace/newlines.
 *
 * @return Heap-allocated key string (caller must free), or NULL.
 */
static char *read_api_key(void) {
  const char *paths[] = {".tmdb_api_key", NULL};
  char home_path[512] = "";

  const char *home = getenv("HOME");
  if (home) {
    snprintf(home_path, sizeof(home_path), "%s/.tmdb_api_key", home);
    paths[1] = home_path;
  }

  for (int i = 0; i < 2; i++) {
    if (!paths[i])
      continue;
    FILE *f = fopen(paths[i], "r");
    if (!f)
      continue;
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
      fclose(f);
      /* Strip trailing whitespace. */
      size_t len = strlen(buf);
      while (len > 0 &&
             (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
              buf[len - 1] == ' '))
        buf[--len] = '\0';
      if (len > 0)
        return strdup(buf);
    }
    fclose(f);
  }
  return NULL;
}

TmdbMovieInfo tmdb_fetch_movie(int tmdb_id) {
  TmdbMovieInfo info = {.error = -1, .release_year = 0};
  info.original_title[0] = '\0';
  info.original_language[0] = '\0';

  char *api_key = read_api_key();
  if (!api_key) {
    fprintf(stderr,
            "Error: TMDB API key not found.\n"
            "Create a .tmdb_api_key file in the current or home directory.\n");
    return info;
  }

  char url[512];
  snprintf(url, sizeof(url),
           "https://api.themoviedb.org/3/movie/%d?api_key=%s", tmdb_id,
           api_key);
  free(api_key);

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
