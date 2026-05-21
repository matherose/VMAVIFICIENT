#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Last component of a path: vmav_path_basename("/a/b/c.mkv", out, n)
 * writes "c.mkv". Trailing slashes are ignored ("/a/b/" -> "b"). The
 * full path is written if no separator is present. Out is always
 * NUL-terminated, even if the source path is NULL/empty. */
void vmav_path_basename(const char *path, char *out, size_t out_size);

/* Directory portion of a path. "a/b/c" -> "a/b". Without separator
 * the result is ".". Trailing slashes are trimmed. */
void vmav_path_dirname(const char *path, char *out, size_t out_size);

/* File extension including the leading dot. "movie.mkv" -> ".mkv".
 * Hidden files (".bashrc") have no extension. The function only
 * inspects the basename portion of the path. */
void vmav_path_extension(const char *path, char *out, size_t out_size);

/* Filename minus extension: "movie.mkv" -> "movie".
 * "/a/b.mp4.bak" -> "b.mp4". */
void vmav_path_stem(const char *path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
