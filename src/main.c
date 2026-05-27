#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_os.h"

#include "commands/commands.h"

const vmav_subcmd_t VMAV_CMD_TABLE[] = {
    {"encode", "Encode a video to AV1 (the main workflow)", cmd_encode_run},
    {"analyze", "Probe a video without encoding", cmd_analyze_run},
    {"search", "TMDB metadata lookup", cmd_search_run},
    {"doctor", "Environment self-check", cmd_doctor_run},
    {"version", "Print version information", cmd_version_run},
    {"help", "Print this message", cmd_help_run},
    {NULL, NULL, NULL},
};

int main(int argc, char **argv) {
    /* Best-effort: on Windows console, enable VT processing so ANSI
     * escapes render. No-op on POSIX. */
    (void)vmav_term_enable_vt();

    /* Honor VMAV_LOG_LEVEL env var; default INFO. */
    vmav_log_level_t lvl = VMAV_LL_INFO;
    const char *lvl_env = vmav_env_get("VMAV_LOG_LEVEL");
    if (lvl_env != NULL) {
        (void)vmav_log_level_from_str(lvl_env, &lvl);
    }
    vmav_log_init(lvl, VMAV_LOG_SINK_STDERR);

    return vmav_cli_dispatch(argc, argv, VMAV_CMD_TABLE, "encode");
}
