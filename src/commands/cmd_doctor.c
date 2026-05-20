#include "commands.h"

#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_os.h"
#include "vmavificient/vmav_subproc.h"
#include "vmavificient/vmav_ui.h"
#include "vmavificient/vmav_version.h"

#include <stdio.h>
#include <string.h>

static void check_ffmpeg(vmav_ui_table_t *t) {
    const char *ffmpeg_argv[] = {"ffmpeg", "-version", NULL};
    vmav_subproc_spec_t spec = {
        .exe = "ffmpeg",
        .argv = ffmpeg_argv,
        .capture_stdout = true,
        .capture_stderr = true,
        .timeout_ms = 3000,
    };
    vmav_subproc_result_t r;
    memset(&r, 0, sizeof(r));
    vmav_status_t st = vmav_subproc_run(&spec, &r);
    if (vmav_status_ok(st) && r.exit_code == 0 && r.stdout_buf.data != NULL) {
        char line[96];
        const size_t n = strcspn(r.stdout_buf.data, "\n");
        snprintf(line,
                 sizeof(line),
                 "%.*s",
                 (int)(n < sizeof(line) - 1 ? n : sizeof(line) - 1),
                 r.stdout_buf.data);
        vmav_ui_table_add(t, "ffmpeg", line);
    } else {
        vmav_ui_table_add(t, "ffmpeg", "not found (will be vendored in Phase 3)");
    }
    vmav_subproc_result_free(&r);
}

static void check_path(vmav_ui_table_t *t, const char *label,
                       vmav_status_t (*fn)(char *, size_t)) {
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

    check_ffmpeg(t);

    vmav_ui_table_render(t, stdout);
    vmav_ui_table_free(t);
    return 0;
}
