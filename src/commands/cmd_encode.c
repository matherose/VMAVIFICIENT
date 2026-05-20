#include "commands.h"

#include <stdio.h>

int cmd_encode_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fputs("vmavificient: 'encode' is not implemented in this Phase 1 skeleton.\n"
          "Real encoder pipeline lands in Phase 4. See "
          "~/.claude/plans/the-project-works-perfectly-proud-clock.md.\n",
          stderr);
    return 2;
}
