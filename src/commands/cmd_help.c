#include "vmavificient/vmav_version.h"

#include "commands.h"

#include <stdio.h>

int cmd_help_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("vmavificient %s — AV1 encoding CLI\n\n", VMAV_VERSION_STRING);
    vmav_cli_render_help(VMAV_CMD_TABLE, stdout);
    puts("\nv2.0.0-dev — see https://github.com/joeltordjman/VMAVIFICIENT for status.");
    return 0;
}
