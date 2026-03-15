/*
 * matcher.c  --  Search the NCD database for matching directories
 */

#include "matcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
    strncpy(buf, search_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "\\/");
    while (tok && sp.count < MAX_PARTS) {
        /* strip leading/trailing whitespace */
        while (*tok && isspace((unsigned char)*tok)) tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';

        if (*tok) {
            strncpy(sp.parts[sp.count], tok, NCD_MAX_NAME - 1);
            sp.parts[sp.count][NCD_MAX_NAME - 1] = '\0';
            sp.count++;
        }
        tok = strtok(NULL, "\\/");
    }
    return sp;
}

/* ============================================= chain matching             */

/*
 * Test whether dir_index in drv is the tip of a chain matching parts[].
 *
 * parts[last] must match drv->dirs[dir_index].name  (substring, CI)
 * parts[last-1] must match its parent's name, etc.
 *
 * Returns true if all parts match as a contiguous parent chain.
 */
static bool chain_matches(const DriveData *drv,
                           int              dir_index,
                           const SearchParts *sp)
{
    if (sp->count == 0) return false;

    int cur = dir_index;
    /* Walk from the deepest part (sp->parts[count-1]) upward */
    for (int i = sp->count - 1; i >= 0; i--) {
        if (cur < 0 || cur >= drv->dir_count) return false;
        if (!istartswith(drv->name_pool + drv->dirs[cur].name_off, sp->parts[i])) return false;
        cur = drv->dirs[cur].parent;   /* -1 when we reach the drive root */
    }
    return true;
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

    int   cap     = 16;
    int   count   = 0;
    NcdMatch *results = xmalloc(sizeof(NcdMatch) * (size_t)cap);

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

    if (count == 0) {
        free(results);
        return NULL;
    }

    *out_count = count;
    return results;
}
