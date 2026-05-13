/**
 * @file tmdb.h
 * @brief TheMovieDB API client for fetching movie metadata.
 */

#ifndef TMDB_H
#define TMDB_H

/**
 * @brief Movie metadata retrieved from TMDB.
 */
typedef struct {
  int error;                 /**< 0 on success, -1 on failure. */
  char original_title[512];  /**< UTF-8 original title. */
  int release_year;          /**< Year extracted from release_date. */
  char original_language[8]; /**< ISO 639-1 code (e.g. "en", "fr"). */
} TmdbMovieInfo;

/**
 * @brief Fetch movie metadata from TMDB by ID.
 *
 * Requires the TMDB_API_KEY environment variable to be set.
 *
 * @param tmdb_id  The TMDB movie ID.
 * @return A @ref TmdbMovieInfo struct with the result.
 */
TmdbMovieInfo tmdb_fetch_movie(int tmdb_id);

#endif /* TMDB_H */
