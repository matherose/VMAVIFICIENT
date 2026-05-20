#include "commands.h"

#include <stdio.h>

int cmd_analyze_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fputs("vmavificient: 'analyze' is not implemented in this Phase 1 skeleton.\n"
          "Probe/media-info pipeline lands in Phase 3.\n",
          stderr);
    return 2;
}
