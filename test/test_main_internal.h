/*
 * test_main_internal.h  --  Internal declarations from main.c for testing
 *
 * This header provides access to functions that have been extracted
 * to separate modules (cli.c, result.c) or exposed via test shims.
 */

#ifndef NCD_TEST_MAIN_INTERNAL_H
#define NCD_TEST_MAIN_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../src/ncd.h"
#include "../src/cli.h"
#include "../src/result.h"
#include <stdbool.h>

/*
 * The following functions are now in separate modules:
 * - cli.h: parse_args, parse_agent_args, glob_match
 * - result.h: write_result, result_ok, result_error, result_cancel
 *             heur_sanitize, heur_promote_match, etc.
 *
 * For tests, just include the module headers above.
 */

/* Additional test helpers that may be needed */
extern void ncd_print(const char *s);
extern void ncd_println(const char *s);
extern void ncd_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* NCD_TEST_MAIN_INTERNAL_H */
