#pragma once

#include "vmavificient/vmav_cli.h"

/* Entry points for each subcommand. Each follows the vmav_subcmd_fn
 * signature; main.c places them in the dispatch table. */

int cmd_version_run(int argc, char **argv);
int cmd_help_run(int argc, char **argv);
int cmd_doctor_run(int argc, char **argv);
int cmd_encode_run(int argc, char **argv);
int cmd_analyze_run(int argc, char **argv);
int cmd_search_run(int argc, char **argv);

/* Shared by main.c so cmd_help can render the same table. */
extern const vmav_subcmd_t VMAV_CMD_TABLE[];
