#include "str_utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool vmav_str_contains_ci(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    const size_t hlen = strlen(haystack);
    const size_t nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }
    if (nlen > hlen) {
        return false;
    }
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool vmav_str_starts_with(const char *s, const char *prefix) {
    if (s == NULL || prefix == NULL) {
        return false;
    }
    const size_t plen = strlen(prefix);
    return strncmp(s, prefix, plen) == 0;
}

bool vmav_str_ends_with(const char *s, const char *suffix) {
    if (s == NULL || suffix == NULL) {
        return false;
    }
    const size_t slen = strlen(s);
    const size_t sfx_len = strlen(suffix);
    if (sfx_len > slen) {
        return false;
    }
    return strcmp(s + slen - sfx_len, suffix) == 0;
}

char *vmav_str_dup(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    const size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

void vmav_str_to_lower(const char *src, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_size && src[i] != '\0'; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}
