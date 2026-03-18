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

/* ================================================================ name index */

typedef struct {
    uint32_t hash;       /* FNV-1a hash of lowercase name */
    uint32_t drive_idx;  /* Index in database drives array */
    uint32_t dir_idx;    /* Index in drive's dirs array */
} NameIndexEntry;

typedef struct {
    NameIndexEntry *entries;
    int count;
    int bucket_count;
    int *bucket_starts;
} NameIndex;

NameIndex *name_index_build(const NcdDatabase *db);
void name_index_free(NameIndex *idx);
int name_index_find_by_hash(const NameIndex *idx, uint32_t hash,
                            NameIndexEntry *out_entries, int max_matches);

/* ================================================================ fuzzy matching */

/*
 * Fuzzy matching with Damerau-Levenshtein distance.
 * 
 * Layer 1: Generates variations of search_str by substituting digits with words
 *          (e.g., "2" -> "to", "too", "two") up to 3 replacements.
 * Layer 2: Ranks candidates using Damerau-Levenshtein distance with normalized
 *          scoring: score = 1.0 - (distance / max(len(query), len(candidate)))
 * 
 * Only returns matches with score >= 0.5 (50% similarity).
 * Results are sorted by score (highest first).
 * 
 * Returns malloc'd array (caller must free) or NULL on error.
 */
NcdMatch *matcher_find_fuzzy(const NcdDatabase *db,
                              const char        *search_str,
                              bool               include_hidden,
                              bool               include_system,
                              int               *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NCD_MATCHER_H */
