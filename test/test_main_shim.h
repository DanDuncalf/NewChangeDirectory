/*
 * test_main_shim.h  --  Shim to expose static functions from main.c for testing
 *
 * This header works by:
 * 1. Redefining "static" to nothing (via the STATIC macro)
 * 2. Including declarations for functions that need to be exposed
 *
 * Use with caution - this is for unit testing only!
 */

#ifndef NCD_TEST_MAIN_SHIM_H
#define NCD_TEST_MAIN_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Redefine static to nothing to expose internal functions */
#ifdef TEST_SHIM
#define STATIC
#else
#define STATIC static
#endif

/* Include the full NcdOptions structure for tests */
#include "../src/ncd.h"

/* Console output functions */
STATIC void ncd_print(const char *s);
STATIC void ncd_println(const char *s);
STATIC void ncd_printf(const char *fmt, ...);

/* Result output functions (also declared in result.h) */
STATIC void write_result(bool ok, const char *drive, const char *path,
                         const char *message);
STATIC void result_error(const char *fmt, ...);
STATIC void result_cancel(void);
STATIC void result_ok(const char *full_path, char drive_letter);

/* Heuristic helpers (also declared in result.h) */
STATIC void heur_sanitize(const char *src, char *dst, size_t dst_size, bool to_lower);
STATIC bool heur_get_preferred(const char *search_raw, char *out_path, size_t out_size);
STATIC void heur_note_choice(const char *search_raw, const char *target_path);
STATIC void heur_promote_match(NcdMatch *matches, int count, const char *preferred_path);
STATIC void heur_print(void);
STATIC void heur_clear(void);

/* CLI parsing (also declared in cli.h) */
STATIC bool parse_args(int argc, char *argv[], NcdOptions *opts);
STATIC bool parse_agent_args(int argc, char *argv[], int *consumed, NcdOptions *opts);

/* Glob matching (also declared in cli.h) */
STATIC bool glob_match(const char *pattern, const char *text);

/* Agent mode functions */
STATIC int agent_mode_query(NcdDatabase *db, const NcdOptions *opts);
STATIC int agent_mode_ls(const NcdOptions *opts);
STATIC int agent_mode_tree(NcdDatabase *db, const NcdOptions *opts);
STATIC int agent_mode_check(NcdDatabase *db, const NcdOptions *opts);
STATIC int agent_mode_mkdir(const NcdOptions *opts);
STATIC int agent_mode_complete(NcdDatabase *db, const NcdOptions *opts);

#ifdef __cplusplus
}
#endif

#endif /* NCD_TEST_MAIN_SHIM_H */
