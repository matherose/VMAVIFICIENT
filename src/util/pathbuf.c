#include "pathbuf.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool is_sep(char c) {
    return c == '/' || c == '\\';
}

__attribute__((format(printf, 3, 4))) static void write_safe(char *out,
                                                             size_t out_size,
                                                             const char *fmt,
                                                             ...) {
    if (out == NULL || out_size == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(out, out_size, fmt, ap);
    va_end(ap);
}

/* Locate the start of the basename within `path`. */
static const char *find_basename_start(const char *path) {
    const char *p = path;
    const char *last_sep = NULL;
    for (; *p != '\0'; p++) {
        if (is_sep(*p)) {
            last_sep = p;
        }
    }
    return (last_sep != NULL) ? last_sep + 1 : path;
}

void vmav_path_basename(const char *path, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }
    if (path == NULL || path[0] == '\0') {
        out[0] = '\0';
        return;
    }
    /* Strip trailing separators (but keep root '/'). */
    size_t end = strlen(path);
    while (end > 1 && is_sep(path[end - 1])) {
        end--;
    }
    /* Find the last separator before `end`. */
    size_t start = 0;
    for (size_t i = 0; i < end; i++) {
        if (is_sep(path[i])) {
            start = i + 1;
        }
    }
    const size_t n = end - start;
    if (n >= out_size) {
        out[0] = '\0';
        return;
    }
    memcpy(out, path + start, n);
    out[n] = '\0';
}

void vmav_path_dirname(const char *path, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }
    if (path == NULL || path[0] == '\0') {
        write_safe(out, out_size, ".");
        return;
    }
    size_t end = strlen(path);
    while (end > 1 && is_sep(path[end - 1])) {
        end--;
    }
    size_t cut = (size_t)-1;
    for (size_t i = 0; i < end; i++) {
        if (is_sep(path[i])) {
            cut = i;
        }
    }
    if (cut == (size_t)-1) {
        write_safe(out, out_size, ".");
        return;
    }
    if (cut == 0) {
        write_safe(out, out_size, "%c", path[0]);
        return;
    }
    const size_t n = cut;
    if (n >= out_size) {
        out[0] = '\0';
        return;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

void vmav_path_extension(const char *path, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }
    if (path == NULL || path[0] == '\0') {
        out[0] = '\0';
        return;
    }
    const char *base = find_basename_start(path);
    if (base[0] == '\0' || base[0] == '.') {
        out[0] = '\0';
        return;
    }
    const char *dot = strrchr(base, '.');
    if (dot == NULL || dot == base) {
        out[0] = '\0';
        return;
    }
    const size_t n = strlen(dot);
    if (n >= out_size) {
        out[0] = '\0';
        return;
    }
    memcpy(out, dot, n + 1);
}

void vmav_path_stem(const char *path, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }
    vmav_path_basename(path, out, out_size);
    char *dot = strrchr(out, '.');
    if (dot != NULL && dot != out) {
        *dot = '\0';
    }
}
