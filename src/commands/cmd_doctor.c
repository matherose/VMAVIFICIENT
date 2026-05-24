#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_os.h"
#include "vmavificient/vmav_ui.h"
#include "vmavificient/vmav_version.h"

#include "commands.h"

#include <stdio.h>
#include <string.h>

static void check_path(vmav_ui_table_t *t, const char *label, vmav_status_t (*fn)(char *, size_t)) {
    char buf[VMAV_PATH_MAX];
    vmav_status_t st = fn(buf, sizeof(buf));
    if (!vmav_status_ok(st)) {
        vmav_ui_table_add(t, label, st.msg);
        return;
    }
    char display[VMAV_PATH_MAX + 32];
    snprintf(display, sizeof(display), "%s%s", buf, vmav_fs_exists(buf) ? " (exists)" : "");
    vmav_ui_table_add(t, label, display);
}

int cmd_doctor_run(int argc, char **argv) {
    (void)argc;
    (void)argv;

    vmav_ui_table_t *t = vmav_ui_table_new("vmavificient doctor");

    char version_line[128];
    snprintf(version_line,
             sizeof(version_line),
             "%s (git %s, built %s)",
             VMAV_VERSION_STRING,
             VMAV_GIT_SHA,
             VMAV_BUILD_DATE);
    vmav_ui_table_add(t, "version", version_line);

    vmav_ui_table_add(t, "log level", vmav_log_level_str(vmav_log_get_level()));
    vmav_ui_table_add(t, "color", vmav_term_no_color() ? "off (NO_COLOR/TERM=dumb)" : "on");

    check_path(t, "config dir", vmav_path_config_dir);
    check_path(t, "cache dir", vmav_path_cache_dir);

    vmav_status_t vt = vmav_term_enable_vt();
    vmav_ui_table_add(t, "vt support", vmav_status_ok(vt) ? "ok" : vt.msg);

    /* No runtime dep checks needed: everything (libavformat / SVT-AV1 /
     * tesseract / libcurl / libopus / etc.) is vendored and statically
     * linked. If we ever add an *optional* runtime tool that augments
     * vmavificient (e.g. dovi_tool for DV RPU injection), check it here. */

    vmav_ui_table_render(t, stdout);
    vmav_ui_table_free(t);
    return 0;
}
