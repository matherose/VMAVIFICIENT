/* Anchor declaration so this translation unit is non-empty on POSIX
 * builds (where the entire body is #if-elided). C11 forbids empty TUs. */
typedef int vmav_os_win_anchor_t;

#if defined(_WIN32)

/* Phase 2 will fill in the Windows implementation in this file. For
 * Phase 1 we publish stubs that return VMAV_ERR_NOT_IMPL so the static
 * link picks them up but they fail at runtime. This lets the Windows
 * cross-compile build green while we focus on the POSIX layer. */

#include "vmavificient/vmav_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *vmav_env_get(const char *name) {
    (void)name;
    return NULL;
}

bool vmav_term_isatty(int fd) {
    (void)fd;
    return false;
}

bool vmav_term_no_color(void) {
    return true;
}

vmav_status_t vmav_term_enable_vt(void) {
    return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_term_enable_vt: Phase 2");
}

bool vmav_fs_exists(const char *path) {
    (void)path;
    return false;
}

bool vmav_fs_is_dir(const char *path) {
    (void)path;
    return false;
}

vmav_status_t vmav_fs_mkdir_p(const char *path) {
    (void)path;
    return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_fs_mkdir_p: Phase 2");
}

vmav_status_t vmav_path_join(char *out, size_t out_size, const char *a, const char *b) {
    (void)out;
    (void)out_size;
    (void)a;
    (void)b;
    return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_path_join: Phase 2");
}

vmav_status_t vmav_path_config_dir(char *out, size_t out_size) {
    (void)out;
    (void)out_size;
    return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_path_config_dir: Phase 2");
}

vmav_status_t vmav_path_cache_dir(char *out, size_t out_size) {
    (void)out;
    (void)out_size;
    return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_path_cache_dir: Phase 2");
}

uint64_t vmav_time_now_ms(void) {
    return 0;
}

vmav_status_t vmav_time_now_iso8601(char *out, size_t out_size) {
    (void)out;
    (void)out_size;
    return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_time_now_iso8601: Phase 2");
}

#endif /* _WIN32 */
