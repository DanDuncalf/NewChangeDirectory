/*
 * matcher.c  --  Search the NCD database for matching directories
 */

#include "matcher.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ hash index */

/* FNV-1a hash - fast, good distribution for strings */
static uint32_t fnv1a_hash(const char *s)
{
    uint32_t hash = 2166136261U;
    while (*s) {
        hash ^= (uint32_t)(unsigned char)tolower((unsigned char)*s);
        hash *= 16777619U;
        s++;
    }
    return hash;
}

/* Comparison for qsort - sort by hash */
static int compare_by_hash(const void *a, const void *b)
{
    const NameIndexEntry *ea = (const NameIndexEntry *)a;
    const NameIndexEntry *eb = (const NameIndexEntry *)b;
    if (ea->hash < eb->hash) return -1;
    if (ea->hash > eb->hash) return 1;
    return 0;
}

/* ================================================================ helpers */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "NCD: out of memory\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) { fprintf(stderr, "NCD: out of memory\n"); exit(1); }
    return p;
}

static void *xcalloc(size_t n, size_t sz)
{
    void *p = calloc(n, sz);
    if (!p) { fprintf(stderr, "NCD: out of memory\n"); exit(1); }
    return p;
}

/* Name index implementation */
NameIndex *name_index_build(const NcdDatabase *db)
{
    int total_dirs = 0;
    for (int di = 0; di < db->drive_count; di++) {
        total_dirs += db->drives[di].dir_count;
    }
    if (total_dirs == 0) return NULL;
    
    NameIndex *idx = xmalloc(sizeof(NameIndex));
    idx->entries = xmalloc(sizeof(NameIndexEntry) * (size_t)total_dirs);
    idx->count = 0;
    idx->bucket_count = 256;
    idx->bucket_starts = NULL;
    
    for (int di = 0; di < db->drive_count; di++) {
        const DriveData *drv = &db->drives[di];
        for (int ei = 0; ei < drv->dir_count; ei++) {
            const char *name = drv->name_pool + drv->dirs[ei].name_off;
            NameIndexEntry *entry = &idx->entries[idx->count++];
            entry->hash = fnv1a_hash(name);
            entry->drive_idx = (uint32_t)di;
            entry->dir_idx = (uint32_t)ei;
        }
    }
    
    qsort(idx->entries, (size_t)idx->count, sizeof(NameIndexEntry), compare_by_hash);
    
    return idx;
}

void name_index_free(NameIndex *idx)
{
    if (!idx) return;
    free(idx->entries);
    free(idx->bucket_starts);
    free(idx);
}

/* Binary search for leftmost occurrence of hash */
static int find_hash_left(const NameIndexEntry *entries, int count, uint32_t hash)
{
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (entries[mid].hash < hash)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int name_index_find_by_hash(const NameIndex *idx, uint32_t hash,
                            NameIndexEntry *out_entries, int max_matches)
{
    if (!idx || !out_entries || max_matches <= 0) return 0;
    
    /* Binary search for first occurrence of hash */
    int start = find_hash_left(idx->entries, idx->count, hash);
    
    /* Collect all entries with matching hash */
    int count = 0;
    for (int i = start; i < idx->count && count < max_matches; i++) {
        if (idx->entries[i].hash == hash) {
            out_entries[count++] = idx->entries[i];
        } else if (idx->entries[i].hash > hash) {
            break;  /* Past the range of matching hashes */
        }
    }
    return count;
}

/*
 * Case-insensitive prefix test: does name START WITH prefix?
 *
 * This is the classic Norton CD matching rule: typing "down" matches
 * "Downloads" but not "my_downloads" or "registry.ollama.ai".
 * Substring matching (the previous behaviour) caused domain-name-style
 * directory entries like "registry.ollama.ai" to appear as false positives
 * when searching for "ollama".
 */
static bool istartswith(const char *name, const char *prefix)
{
    if (!prefix || prefix[0] == '\0') return true;
    size_t plen = strlen(prefix);
    size_t nlen = strlen(name);
    if (plen > nlen) return false;
    for (size_t i = 0; i < plen; i++) {
        if (tolower((unsigned char)name[i]) !=
            tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

/* ============================================= parse search into parts    */

#define MAX_PARTS 32

typedef struct {
    char   parts[MAX_PARTS][NCD_MAX_NAME];
    int    count;
} SearchParts;

/*
 * Split search_str on '\' or '/' into parts[].
 * e.g. "scott\downloads" -> ["scott", "downloads"]
 */
static SearchParts parse_search(const char *search_str)
{
    SearchParts sp;
    memset(&sp, 0, sizeof(sp));

    char buf[NCD_MAX_PATH];
    platform_strncpy_s(buf, sizeof(buf), search_str);

    char *saveptr = NULL;
    char *tok = platform_strtok(buf, "\\/", &saveptr);
    while (tok && sp.count < MAX_PARTS) {
        /* strip leading/trailing whitespace */
        while (*tok && isspace((unsigned char)*tok)) tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';

        if (*tok) {
            platform_strncpy_s(sp.parts[sp.count], NCD_MAX_NAME, tok);
            sp.count++;
        }
        tok = platform_strtok(NULL, "\\/", &saveptr);
    }
    return sp;
}

/* ============================================= chain matching             */

/*
 * Test whether dir_index in drv is the tip of a chain matching parts[].
 *
 * parts[last] must match drv->dirs[dir_index].name  (substring, CI)
 * parts[0] must match some ancestor's name (substring, CI)
 *
 * Intermediate directories between parts are allowed (non-contiguous matching).
 * e.g., "Users\Downloads" matches "C:\Users\Scott\Downloads"
 *
 * Returns true if all parts match somewhere in the parent chain.
 */
static bool chain_matches(const DriveData *drv,
                           int              dir_index,
                           const SearchParts *sp)
{
    if (sp->count == 0) return false;

    /* Leaf (last part) must match the starting directory */
    if (dir_index < 0 || dir_index >= drv->dir_count) return false;
    if (!istartswith(drv->name_pool + drv->dirs[dir_index].name_off, sp->parts[sp->count - 1]))
        return false;

    /* For single-part search, leaf match is sufficient */
    if (sp->count == 1) return true;

    /* 
     * For multi-part search, walk up the parent chain looking for matches
     * with earlier parts. We need to find parts[count-2], then parts[count-3], etc.
     * Intermediate directories are skipped (non-contiguous matching).
     */
    int parts_idx = sp->count - 2;  /* Start with second-to-last part */
    int cur = drv->dirs[dir_index].parent;
    
    while (cur >= 0 && parts_idx >= 0) {
        if (istartswith(drv->name_pool + drv->dirs[cur].name_off, sp->parts[parts_idx])) {
            parts_idx--;  /* Found this part, move to next one */
        }
        cur = drv->dirs[cur].parent;
    }
    
    /* Success if we found all parts */
    return parts_idx < 0;
}

/* ================================================================ public   */

NcdMatch *matcher_find(const NcdDatabase *db,
                        const char        *search_str,
                        bool               include_hidden,
                        bool               include_system,
                        int               *out_count)
{
    *out_count = 0;
    if (!db || !search_str || !search_str[0]) return NULL;

    SearchParts sp = parse_search(search_str);
    if (sp.count == 0) return NULL;

    /* The leaf component is the last part */
    const char *leaf = sp.parts[sp.count - 1];
    uint32_t leaf_hash = fnv1a_hash(leaf);

    /* Build index for this database (no caching - prevents use-after-free) */
    NameIndex *idx = name_index_build(db);

    int   cap     = 16;
    int   count   = 0;
    NcdMatch *results = xmalloc(sizeof(NcdMatch) * (size_t)cap);

    if (idx) {
        /* Use hash index for fast lookup */
        NameIndexEntry matches[256];
        int nmatches = name_index_find_by_hash(idx, leaf_hash, matches, 256);
        
        for (int mi = 0; mi < nmatches; mi++) {
            const NameIndexEntry *nie = &matches[mi];
            const DriveData *drv = &db->drives[nie->drive_idx];
            const DirEntry *e = &drv->dirs[nie->dir_idx];

            /* Apply visibility filters */
            if (e->is_hidden && !include_hidden) continue;
            if (e->is_system && !include_system) continue;

            /* Verify prefix match (hash collision check) */
            if (!istartswith(drv->name_pool + e->name_off, leaf)) continue;

            /* Full chain check */
            if (!chain_matches(drv, (int)nie->dir_idx, &sp)) continue;

            /* Record this match */
            if (count >= cap) {
                cap *= 2;
                results = xrealloc(results, sizeof(NcdMatch) * (size_t)cap);
            }
            NcdMatch *m = &results[count++];
            m->drive_letter = drv->letter;
            m->drive_index  = (int)nie->drive_idx;
            m->dir_index    = (int)nie->dir_idx;
            db_full_path(drv, (int)nie->dir_idx, m->full_path, sizeof(m->full_path));
        }
    }
    
    /* 
     * Fall back to full scan if:
     * - Index not available (OOM), OR
     * - Hash index returned no matches (for prefix searches like "NewCh" 
     *   that don't match any full directory name hash)
     */
    if (count == 0) {
        for (int di = 0; di < db->drive_count; di++) {
            const DriveData *drv = &db->drives[di];

            for (int ei = 0; ei < drv->dir_count; ei++) {
                const DirEntry *e = &drv->dirs[ei];

                /* Apply visibility filters */
                if (e->is_hidden && !include_hidden) continue;
                if (e->is_system && !include_system) continue;

                /* Quick leaf check before full chain walk */
                if (!istartswith(drv->name_pool + e->name_off, leaf)) continue;

                /* Full chain check */
                if (!chain_matches(drv, ei, &sp)) continue;

                /* Record this match */
                if (count >= cap) {
                    cap *= 2;
                    results = xrealloc(results, sizeof(NcdMatch) * (size_t)cap);
                }
                NcdMatch *m = &results[count++];
                m->drive_letter = drv->letter;
                m->drive_index  = di;
                m->dir_index    = ei;
                db_full_path(drv, ei, m->full_path, sizeof(m->full_path));
            }
        }
    }

    name_index_free(idx);

    if (count == 0) {
        free(results);
        return NULL;
    }

    *out_count = count;
    return results;
}

/* ================================================================ fuzzy matching */

/*
 * Digit-word substitution table (bidirectional)
 * Each digit maps to its word equivalents and vice versa
 */
static const char *digit_to_words[10][4] = {
    {"zero", "oh", "o", NULL},        /* 0 */
    {"one", "won", NULL, NULL},       /* 1 */
    {"to", "too", "two", NULL},       /* 2 */
    {"three", NULL, NULL, NULL},      /* 3 */
    {"for", "fore", "four", NULL},    /* 4 */
    {"five", NULL, NULL, NULL},       /* 5 */
    {"six", NULL, NULL, NULL},        /* 6 */
    {"seven", NULL, NULL, NULL},      /* 7 */
    {"ate", "eight", NULL, NULL},     /* 8 */
    {"nine", NULL, NULL, NULL},       /* 9 */
};

/* Maximum variations to generate per search term */
#define MAX_VARIATIONS 256
#define MAX_SUBSTITUTIONS 3
#define DL_THRESHOLD 0.5

typedef struct {
    char *variations[MAX_VARIATIONS];
    int count;
} VariationList;

/*
 * Check if a character is a digit
 */
static bool is_digit_char(char c)
{
    return c >= '0' && c <= '9';
}

/*
 * Convert a digit char to its numeric value
 */
static int digit_char_value(char c)
{
    return c - '0';
}

/*
 * Check if a word matches a digit at a position in the string
 * Returns the word length if matched, 0 otherwise
 */
static int match_word_at(const char *str, const char *word)
{
    size_t word_len = strlen(word);
    size_t str_len = strlen(str);
    if (word_len > str_len) return 0;
    
    for (size_t i = 0; i < word_len; i++) {
        if (tolower((unsigned char)str[i]) != word[i])
            return 0;
    }
    return (int)word_len;
}

/*
 * Generate all variations of a search term with up to max_replacements substitutions
 * This includes both digit->word and word->digit substitutions
 */
static void generate_variations_recursive(const char *input, int pos, int replacements,
                                           char *current, VariationList *out,
                                           int max_replacements)
{
    if (replacements > max_replacements) return;
    if (pos >= (int)strlen(input)) {
        /* End of string - add to variations if not empty */
        if (strlen(current) > 0 && out->count < MAX_VARIATIONS) {
            out->variations[out->count] = xmalloc(strlen(current) + 1);
            strcpy(out->variations[out->count], current);
            out->count++;
        }
        return;
    }
    
    char c = input[pos];
    size_t cur_len = strlen(current);
    
    /* Option 1: Keep character as-is */
    current[cur_len] = c;
    current[cur_len + 1] = '\0';
    generate_variations_recursive(input, pos + 1, replacements, current, out, max_replacements);
    current[cur_len] = '\0';  /* Backtrack */
    
    /* Option 2: If current char is a digit, try word substitutions */
    if (is_digit_char(c) && replacements < max_replacements) {
        int digit = digit_char_value(c);
        for (int w = 0; digit_to_words[digit][w] != NULL; w++) {
            const char *word = digit_to_words[digit][w];
            strcat(current, word);
            generate_variations_recursive(input, pos + 1, replacements + 1, current, out, max_replacements);
            current[cur_len] = '\0';  /* Backtrack */
        }
    }
    
    /* Option 3: Try word->digit substitutions (check if a word matches at this position) */
    if (replacements < max_replacements) {
        for (int d = 0; d <= 9; d++) {
            for (int w = 0; digit_to_words[d][w] != NULL; w++) {
                int word_len = match_word_at(input + pos, digit_to_words[d][w]);
                if (word_len > 0) {
                    current[cur_len] = '0' + d;
                    current[cur_len + 1] = '\0';
                    generate_variations_recursive(input, pos + word_len, replacements + 1, 
                                                   current, out, max_replacements);
                    current[cur_len] = '\0';  /* Backtrack */
                }
            }
        }
    }
}

/*
 * Generate all variations of a search term
 */
static VariationList generate_variations(const char *search)
{
    VariationList out;
    out.count = 0;
    
    char current[NCD_MAX_PATH] = {0};
    generate_variations_recursive(search, 0, 0, current, &out, MAX_SUBSTITUTIONS);
    
    return out;
}

/*
 * Free variation list memory
 */
static void free_variations(VariationList *vl)
{
    for (int i = 0; i < vl->count; i++) {
        free(vl->variations[i]);
    }
    vl->count = 0;
}

/*
 * Damerau-Levenshtein distance calculation
 * Handles insertions, deletions, substitutions, and transpositions
 */
static int dl_distance(const char *s1, const char *s2)
{
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    
    if (len1 == 0) return (int)len2;
    if (len2 == 0) return (int)len1;
    
    /* Use two rows for space efficiency */
    int *prev = xmalloc((len2 + 1) * sizeof(int));
    int *curr = xmalloc((len2 + 1) * sizeof(int));
    
    /* Initialize first row */
    for (size_t j = 0; j <= len2; j++) {
        prev[j] = (int)j;
    }
    
    int prev_s1_char = 0;  /* For transposition tracking */
    
    for (size_t i = 1; i <= len1; i++) {
        curr[0] = (int)i;
        int s1_char = tolower((unsigned char)s1[i - 1]);
        
        for (size_t j = 1; j <= len2; j++) {
            int s2_char = tolower((unsigned char)s2[j - 1]);
            int cost = (s1_char == s2_char) ? 0 : 1;
            
            /* Standard Levenshtein operations */
            int deletion = prev[j] + 1;
            int insertion = curr[j - 1] + 1;
            int substitution = prev[j - 1] + cost;
            
            int min = deletion;
            if (insertion < min) min = insertion;
            if (substitution < min) min = substitution;
            
            /* Damerau transposition: check if current chars are swapped */
            if (i > 1 && j > 1 && s1_char == tolower((unsigned char)s2[j - 2]) &&
                prev_s1_char == s2_char) {
                int transposition = prev[j - 2] + cost;
                if (transposition < min) min = transposition;
            }
            
            curr[j] = min;
        }
        
        /* Swap rows */
        int *tmp = prev;
        prev = curr;
        curr = tmp;
        prev_s1_char = s1_char;
    }
    
    int result = prev[len2];
    free(prev);
    free(curr);
    return result;
}

/*
 * Calculate normalized DL similarity score
 * Returns 0.0 to 1.0 where 1.0 is perfect match
 */
static double dl_score(const char *s1, const char *s2)
{
    int dist = dl_distance(s1, s2);
    int max_len = (int)strlen(s1);
    int len2 = (int)strlen(s2);
    if (len2 > max_len) max_len = len2;
    
    if (max_len == 0) return 1.0;
    
    return 1.0 - ((double)dist / (double)max_len);
}

/*
 * Structure for scored match
 */
typedef struct {
    NcdMatch match;
    double score;
} ScoredMatch;

/*
 * Comparison for qsort - sort by score descending
 */
static int compare_scored(const void *a, const void *b)
{
    const ScoredMatch *sa = (const ScoredMatch *)a;
    const ScoredMatch *sb = (const ScoredMatch *)b;
    if (sa->score > sb->score) return -1;
    if (sa->score < sb->score) return 1;
    return 0;
}

/*
 * Fuzzy matching with Damerau-Levenshtein distance
 */
NcdMatch *matcher_find_fuzzy(const NcdDatabase *db,
                              const char        *search_str,
                              bool               include_hidden,
                              bool               include_system,
                              int               *out_count)
{
    *out_count = 0;
    if (!db || !search_str || !search_str[0]) return NULL;
    
    /* Layer 1: Generate variations */
    VariationList variations = generate_variations(search_str);
    if (variations.count == 0) return NULL;
    
    /* Collect all candidates with scores */
    int cap = 64;
    int count = 0;
    ScoredMatch *scored = xmalloc(sizeof(ScoredMatch) * (size_t)cap);
    
    /* Score each directory path against all variations, keep best */
    for (int di = 0; di < db->drive_count; di++) {
        const DriveData *drv = &db->drives[di];
        
        for (int ei = 0; ei < drv->dir_count; ei++) {
            const DirEntry *e = &drv->dirs[ei];
            
            /* Apply visibility filters */
            if (e->is_hidden && !include_hidden) continue;
            if (e->is_system && !include_system) continue;
            
            /* Get full path for comparison */
            char full_path[NCD_MAX_PATH];
            db_full_path(drv, ei, full_path, sizeof(full_path));
            
            /* Find best score across all variations */
            double best_score = 0.0;
            for (int v = 0; v < variations.count; v++) {
                double score = dl_score(variations.variations[v], full_path);
                if (score > best_score) best_score = score;
                
                /* Also try matching just the directory name */
                const char *name = drv->name_pool + e->name_off;
                double name_score = dl_score(variations.variations[v], name);
                if (name_score > best_score) best_score = name_score;
            }
            
            /* Keep if above threshold */
            if (best_score >= DL_THRESHOLD) {
                if (count >= cap) {
                    cap *= 2;
                    scored = xrealloc(scored, sizeof(ScoredMatch) * (size_t)cap);
                }
                scored[count].score = best_score;
                scored[count].match.drive_letter = drv->letter;
                scored[count].match.drive_index = di;
                scored[count].match.dir_index = ei;
                platform_strncpy_s(scored[count].match.full_path, sizeof(scored[count].match.full_path), full_path);
                count++;
            }
        }
    }
    
    free_variations(&variations);
    
    if (count == 0) {
        free(scored);
        return NULL;
    }
    
    /* Sort by score descending */
    qsort(scored, (size_t)count, sizeof(ScoredMatch), compare_scored);
    
    /* Extract just the matches */
    NcdMatch *results = xmalloc(sizeof(NcdMatch) * (size_t)count);
    for (int i = 0; i < count; i++) {
        results[i] = scored[i].match;
    }
    
    free(scored);
    *out_count = count;
    return results;
}
