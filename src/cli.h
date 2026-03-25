/*
 * cli.h  --  Command-line interface parsing for NCD
 *
 * This module handles parsing of command-line arguments for both
 * normal mode and agent mode.
 */

#ifndef NCD_CLI_H
#define NCD_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"
#include <stdbool.h>

/* Agent mode subcommand identifiers */
#define AGENT_SUB_NONE      0
#define AGENT_SUB_QUERY     1
#define AGENT_SUB_LS        2
#define AGENT_SUB_TREE      3
#define AGENT_SUB_CHECK     4
#define AGENT_SUB_COMPLETE  5
#define AGENT_SUB_MKDIR     6
#define AGENT_SUB_MKDIRS    8
#define AGENT_SUB_QUIT      7

/*
 * parse_args  --  Parse command-line arguments
 *
 * argc, argv: Standard main() arguments
 * opts: Output structure to fill with parsed options
 *
 * Returns true on success, false on error (invalid arguments).
 */
bool parse_args(int argc, char *argv[], NcdOptions *opts);

/*
 * parse_agent_args  --  Parse agent mode subcommand arguments
 *
 * argc, argv: Remaining arguments after "/agent"
 * consumed: Output - number of arguments consumed
 * opts: Output structure to fill with parsed options
 *
 * Returns true on success, false on error.
 */
bool parse_agent_args(int argc, char *argv[], int *consumed, NcdOptions *opts);

/*
 * glob_match  --  Simple glob pattern matching
 *
 * Supports: * (match any sequence), ? (match single char)
 * Case-insensitive matching.
 *
 * Returns true if text matches pattern.
 */
bool glob_match(const char *pattern, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* NCD_CLI_H */
