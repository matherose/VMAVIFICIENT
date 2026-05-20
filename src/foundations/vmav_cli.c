#include "vmavificient/vmav_cli.h"

#include <stdio.h>
#include <string.h>

const vmav_subcmd_t *vmav_cli_find(const vmav_subcmd_t *cmds, const char *name) {
    if (cmds == NULL || name == NULL) {
        return NULL;
    }
    for (const vmav_subcmd_t *c = cmds; c->name != NULL; c++) {
        if (strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return NULL;
}

void vmav_cli_render_help(const vmav_subcmd_t *cmds, FILE *out) {
    if (cmds == NULL || out == NULL) {
        return;
    }
    fputs("Usage:\n", out);
    fputs("  vmavificient <subcommand> [options]\n", out);
    fputs("  vmavificient <input>                (one-shot, dispatches to `encode`)\n\n", out);
    fputs("Subcommands:\n", out);
    size_t name_width = 0;
    for (const vmav_subcmd_t *c = cmds; c->name != NULL; c++) {
        const size_t n = strlen(c->name);
        if (n > name_width) {
            name_width = n;
        }
    }
    for (const vmav_subcmd_t *c = cmds; c->name != NULL; c++) {
        fprintf(out,
                "  %-*s   %s\n",
                (int)name_width,
                c->name,
                c->short_help != NULL ? c->short_help : "");
    }
}

int vmav_cli_dispatch(int argc, char **argv, const vmav_subcmd_t *cmds, const char *default_cmd) {
    if (cmds == NULL) {
        return 1;
    }
    if (argc < 2) {
        const vmav_subcmd_t *help = vmav_cli_find(cmds, "help");
        return help != NULL ? help->run(argc, argv) : 0;
    }
    const char *first = argv[1];

    if (strcmp(first, "--version") == 0 || strcmp(first, "-v") == 0) {
        const vmav_subcmd_t *v = vmav_cli_find(cmds, "version");
        if (v != NULL) {
            return v->run(argc - 1, argv + 1);
        }
    }
    if (strcmp(first, "--help") == 0 || strcmp(first, "-h") == 0) {
        const vmav_subcmd_t *h = vmav_cli_find(cmds, "help");
        if (h != NULL) {
            return h->run(argc - 1, argv + 1);
        }
    }

    const vmav_subcmd_t *found = vmav_cli_find(cmds, first);
    if (found != NULL) {
        return found->run(argc - 1, argv + 1);
    }

    /* Backward-compat: not a subcommand, not a flag — treat as
     * positional input for default_cmd. */
    if (first[0] != '-' && default_cmd != NULL) {
        const vmav_subcmd_t *dflt = vmav_cli_find(cmds, default_cmd);
        if (dflt != NULL) {
            return dflt->run(argc, argv);
        }
    }

    fprintf(stderr, "vmavificient: unknown subcommand '%s'\n\n", first);
    vmav_cli_render_help(cmds, stderr);
    return 1;
}
