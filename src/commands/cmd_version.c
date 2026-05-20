#include "vmavificient/vmav_version.h"

#include "commands.h"

#include <stdio.h>

int cmd_version_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf(
        "vmavificient %s (git %s, built %s)\n", VMAV_VERSION_STRING, VMAV_GIT_SHA, VMAV_BUILD_DATE);
    return 0;
}
