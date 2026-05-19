#include "vmavificient/vmav_version.h"

#include <stdio.h>
#include <string.h>

static int cmd_version(void) {
    printf("vmavificient %s (git %s, built %s)\n",
           VMAV_VERSION_STRING, VMAV_GIT_SHA, VMAV_BUILD_DATE);
    return 0;
}

static int cmd_help(FILE *out) {
    fputs(
        "vmavificient " VMAV_VERSION_STRING " — AV1 encoding CLI\n"
        "\n"
        "Usage:\n"
        "  vmavificient <subcommand> [options]\n"
        "  vmavificient <input>                (one-shot, dispatches to `encode`)\n"
        "\n"
        "Subcommands:\n"
        "  encode <input>      Encode a video to AV1 (not implemented in Phase 0)\n"
        "  analyze <input>     Probe a video without encoding (not implemented in Phase 0)\n"
        "  search <title>      TMDB metadata lookup (not implemented in Phase 0)\n"
        "  doctor              Environment self-check (not implemented in Phase 0)\n"
        "  version             Print version information\n"
        "  help                Print this message\n"
        "\n"
        "v2.0.0-dev — see https://github.com/joeltordjman/VMAVIFICIENT for status.\n",
        out);
    return 0;
}

static int cmd_not_implemented(const char *name) {
    fprintf(stderr,
            "vmavificient: '%s' is not implemented in this Phase 0 skeleton.\n"
            "See docs/porting-notes.md for the implementation roadmap.\n",
            name);
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return cmd_help(stdout);
    }
    const char *cmd = argv[1];

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0
        || strcmp(cmd, "-v") == 0) {
        return cmd_version();
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0
        || strcmp(cmd, "-h") == 0) {
        return cmd_help(stdout);
    }
    if (strcmp(cmd, "encode") == 0 || strcmp(cmd, "analyze") == 0
        || strcmp(cmd, "search") == 0 || strcmp(cmd, "doctor") == 0) {
        return cmd_not_implemented(cmd);
    }

    fprintf(stderr, "vmavificient: unknown subcommand '%s'\n", cmd);
    cmd_help(stderr);
    return 1;
}
