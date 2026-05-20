#include "commands.h"

#include <stdio.h>

int cmd_search_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fputs("vmavificient: 'search' is not implemented in this Phase 1 skeleton.\n"
          "TMDB integration lands in Phase 3.\n",
          stderr);
    return 2;
}
