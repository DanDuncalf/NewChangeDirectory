/*
 * matcher.h  --  Match a user search string against the NCD database
 *
 * Search syntax
 * -------------
 * The search string is a partial path with one or more backslash-separated
 * components, e.g.:
 *
 *   downloads           -- find any dir named "downloads"
 *   scott\downloads     -- find "downloads" whose ancestor chain includes
 *                          "scott" immediately above it
 *   users\scott\dl      -- three-level chain; each level is a substring
 *                          (case-insensitive) of the actual directory name
 *
 * Matching rules
 * --------------
 *  1. Component matching is case-insensitive and substring-based:
 *       "down" matches "Downloads"
 *  2. Components must appear as a *contiguous* chain in the directory tree
 *     (direct parent→child relationships, bottom-up).
 *  3. Both forward slash '/' and backslash '\' are accepted as separators.
 *  4. Hidden / system dirs are included or excluded per the options flags
 *     that were applied at scan time (the caller pre-filters if needed).
 */

#ifndef NCD_MATCHER_H
#define NCD_MATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"
#include "database.h"

/*
 * Find all directories in db that match search_str.
 *
 * Returns a malloc'd array of NcdMatch (caller must free(*out_matches)).
 * *out_count is set to the number of matches.
 * Returns NULL (and *out_count == 0) on memory error or no match.
 *
 * include_hidden / include_system -- when false, entries that are
 * flagged as hidden/system are excluded even if they are in the db.
 */
NcdMatch *matcher_find(const NcdDatabase *db,
                        const char        *search_str,
                        bool               include_hidden,
                        bool               include_system,
                        int               *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NCD_MATCHER_H */
