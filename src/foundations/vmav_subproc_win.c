/* Anchor declaration so this TU is non-empty on POSIX builds. */
typedef int vmav_subproc_win_anchor_t;

#if defined(_WIN32)

/* Phase 2 stub. Real CreateProcessW + anonymous pipes + timeout impl
 * arrives in Phase 2. Until then, the Windows build links these
 * symbols but every call fails at runtime with VMAV_ERR_NOT_IMPL. */

#include "vmavificient/vmav_subproc.h"

#include <stdlib.h>
#include <string.h>

vmav_status_t vmav_subproc_run(const vmav_subproc_spec_t *spec, vmav_subproc_result_t *out) {
    (void)spec;
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    return VMAV_ERR(VMAV_ERR_NOT_IMPL, "vmav_subproc_run: Phase 2");
}

void vmav_subproc_result_free(vmav_subproc_result_t *out) {
    if (out == NULL) {
        return;
    }
    free(out->stdout_buf.data);
    free(out->stderr_buf.data);
    memset(out, 0, sizeof(*out));
}

#endif /* _WIN32 */
