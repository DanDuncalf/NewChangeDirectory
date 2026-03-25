/*
 * result.h  --  Result output functions for NCD
 *
 * This module handles writing the result file that communicates
 * back to the shell wrapper (ncd.bat / ncd).
 */

#ifndef NCD_RESULT_H
#define NCD_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"

/*
 * write_result  --  Write the result file for shell wrapper
 *
 * ok: true for success, false for error
 * drive: Drive letter string (e.g., "C:") or empty
 * path: Full path string
 * message: Human-readable message
 *
 * Writes to %TEMP%/ncd_result.bat (Windows) or /tmp/ncd_result.sh (Linux)
 */
void write_result(bool ok, const char *drive, const char *path, const char *message);

/*
 * result_ok  --  Write success result
 *
 * full_path: The full path that was selected
 * drive_letter: The drive letter (Windows) or ignored (Linux)
 */
void result_ok(const char *full_path, char drive_letter);

/*
 * result_error  --  Write error result
 *
 * fmt: printf-style format string
 * ...: variadic arguments
 */
void result_error(const char *fmt, ...);

/*
 * result_cancel  --  Write cancel result
 */
void result_cancel(void);

/* ============================================================= heuristic helpers */

/*
 * heur_sanitize  --  Sanitize search term for heuristic matching
 *
 * src: Source string
 * dst: Destination buffer
 * dst_size: Size of destination buffer
 * to_lower: If true, convert to lowercase
 */
void heur_sanitize(const char *src, char *dst, size_t dst_size, bool to_lower);

/*
 * heur_get_preferred  --  Get preferred path from heuristics
 *
 * search_raw: The raw search term
 * out_path: Output buffer for preferred path
 * out_size: Size of output buffer
 *
 * Returns true if a preferred path was found.
 */
bool heur_get_preferred(const char *search_raw, char *out_path, size_t out_size);

/*
 * heur_note_choice  --  Record a user choice in heuristics
 *
 * search_raw: The search term used
 * target_path: The path that was selected
 */
void heur_note_choice(const char *search_raw, const char *target_path);

/*
 * heur_promote_match  --  Promote preferred match to top of list
 *
 * matches: Array of matches
 * count: Number of matches
 * preferred_path: The path to promote
 */
void heur_promote_match(NcdMatch *matches, int count, const char *preferred_path);

/*
 * heur_print  --  Print heuristics (for /f command)
 */
void heur_print(void);

/*
 * heur_clear  --  Clear all heuristics (for /fc command)
 */
void heur_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* NCD_RESULT_H */
