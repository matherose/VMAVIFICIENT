/**
 * @file tmdb.h
 * @brief TheMovieDB API client for fetching movie and TV metadata.
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

/**
 * @brief TV show metadata retrieved from TMDB.
 */
typedef struct {
  int error;                 /**< 0 on success, -1 on failure. */
  char original_name[512];   /**< UTF-8 original show name. */
  int first_air_year;        /**< Year extracted from first_air_date (0 if absent). */
  char original_language[8]; /**< ISO 639-1 code (e.g. "en", "ja"). */
} TmdbTvInfo;

/**
 * @brief Single-episode metadata retrieved from TMDB.
 */
typedef struct {
  int error;      /**< 0 on success, -1 on failure. */
  char name[512]; /**< UTF-8 episode title (localized, default English). */
} TmdbEpisodeInfo;

/**
 * @brief Fetch TV show metadata from TMDB by series ID.
 * @param tmdb_id  The TMDB TV series ID (themoviedb.org/tv/<id>).
 */
TmdbTvInfo tmdb_fetch_tv(int tmdb_id);

/**
 * @brief Fetch one episode's metadata. Failure is expected to be treated
 *        as non-fatal by callers (missing title just omits the segment).
 */
TmdbEpisodeInfo tmdb_fetch_episode(int tmdb_id, int season, int episode);

#endif /* TMDB_H */
