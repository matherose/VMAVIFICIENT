#pragma once

#include "vmavificient/vmav_result.h"

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each subcommand runs as a thin function that takes (argc, argv) of
 * just its own slice — argv[0] is the subcommand name, argv[1..] are
 * its flags/positionals. Return value becomes the process exit code. */
typedef int (*vmav_subcmd_fn)(int argc, char **argv);

typedef struct {
    const char *name;       /* "encode", "analyze", ... ; NULL terminates table */
    const char *short_help; /* one-line description for --help */
    vmav_subcmd_fn run;
} vmav_subcmd_t;

/* Look up by name. Returns NULL if not found. Table must be terminated
 * by an entry whose `name` is NULL. */
const vmav_subcmd_t *vmav_cli_find(const vmav_subcmd_t *cmds, const char *name);

/* Dispatch. argc/argv are the raw process args.
 *
 *   - `vmavificient version`        -> cmds["version"].run
 *   - `vmavificient --version`      -> cmds["version"].run
 *   - `vmavificient` (no args)      -> cmds["help"].run
 *   - `vmavificient <known>` ...    -> cmds[<known>].run with argv shifted
 *   - `vmavificient <unknown>` ...  -> if <unknown> doesn't start with '-'
 *                                       AND default_cmd is non-NULL, dispatch
 *                                       to default_cmd with full argv;
 *                                       otherwise prints help to stderr +
 *                                       returns 1.
 *
 * Returns the subcommand's exit code, or 1 on dispatch failure. */
int vmav_cli_dispatch(int argc,
                      char **argv,
                      const vmav_subcmd_t *cmds,
                      const char *default_cmd);

/* Render the top-level --help message listing every subcommand. */
void vmav_cli_render_help(const vmav_subcmd_t *cmds, FILE *out);

#ifdef __cplusplus
}
#endif
