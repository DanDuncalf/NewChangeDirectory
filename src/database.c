/*
 * database.c  --  Load, save, and manipulate the NCD directory database
 *
 * Storage format (compact JSON, written on one line):
 * {
 *   "version":  1,
 *   "lastScan": <unix-timestamp>,
 *   "options":  { "showHidden": false, "showSystem": false },
 *   "drives": [
 *     {
 *       "letter": "C",
 *       "label":  "Windows",
 *       "type":   3,
 *       "dirs": [
 *         { "parent": -1, "name": "Users",     "h": 0, "s": 0 },
 *         { "parent":  0, "name": "Scott",     "h": 0, "s": 0 },
 *         { "parent":  1, "name": "Downloads", "h": 0, "s": 0 }
 *       ]
 *     }
 *   ]
 * }
 *
 * Key design decisions vs. the previous version:
 *
 *  1. No JSON tree.
 *     The old code built a full JsonNode/JsonPair tree in memory (~1.7 M
 *     heap allocations for a 100 K-directory database), then walked it once
 *     and discarded it.  db_load now uses a zero-allocation streaming reader
 *     (Rdr) that fills DirEntry arrays directly.  db_save writes JSON text
 *     straight into a growing string buffer with no intermediate tree.
 *
 *  2. String pool.
 *     DirEntry no longer carries a char name[256] field.  All names for a
 *     given drive are packed into a single heap buffer (DriveData.name_pool)
 *     and each DirEntry stores only a uint32_t byte offset.  sizeof(DirEntry)
 *     drops from 268 bytes to 12 bytes.
 *
 *  3. No redundant id field.
 *     The old DirEntry.id was always equal to the array index.  It has been
 *     removed from both the struct and the saved file.  Old database files
 *     that still contain an "id" key are silently tolerated during load.
 */

#include "database.h"
#include "platform.h"
#include "strbuilder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if NCD_PLATFORM_LINUX
#include <sys/stat.h>
#include <sys/types.h>
#endif

/* ================================================================ helpers */

/* Portable compile-time assert for compilers that don't support _Static_assert
 * MSVC historically doesn't support _Static_assert in C mode, so fall back
 * to a typedef-sized array trick.
 */
#ifndef STATIC_ASSERT
#ifdef _MSC_VER
#define STATIC_ASSERT(cond, msg) typedef char static_assertion_at_line_##__LINE__[(cond)?1:-1]
#else
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
#endif


/* Note: xmalloc/xrealloc replaced with ncd_malloc/ncd_realloc from common.c
 *       SB (string builder) replaced with StringBuilder from strbuilder.h */

/* =============================================================== lifecycle */

NcdDatabase *db_create(void)
{
    NcdDatabase *db = ncd_malloc(sizeof(NcdDatabase));
    memset(db, 0, sizeof(NcdDatabase));
    db->version = NCD_VERSION;
    return db;
}

void db_free(NcdDatabase *db)
{
    if (!db) return;
    if (db->is_blob) {
        /*
         * Binary-blob load: dirs[] and name_pool pointers inside each
         * DriveData point directly into blob_buf -- do NOT free them
         * individually.  Just free the two separately-allocated blocks.
         */
        free(db->drives);
        free(db->blob_buf);
    } else {
        for (int i = 0; i < db->drive_count; i++) {
            free(db->drives[i].dirs);
            free(db->drives[i].name_pool);
        }
        free(db->drives);
    }
    free(db);
}

/* ============================================================ path helper */

char *db_default_path(char *buf, size_t buf_size)
{
    if (platform_db_default_path(buf, buf_size)) return buf;
    return NULL;
}

char *db_drive_path(char letter, char *buf, size_t buf_size)
{
    if (platform_db_drive_path(letter, buf, buf_size)) return buf;
    return NULL;
}

bool db_save_binary_single(const NcdDatabase *db, int drive_idx,
                            const char *path)
{
    if (drive_idx < 0 || drive_idx >= db->drive_count) return false;
    const DriveData *drv = &db->drives[drive_idx];

    /* ---- compute total output size (same layout as db_save_binary) ---- */
    size_t dirs_bytes = (size_t)drv->dir_count * sizeof(DirEntry);
    size_t used       = dirs_bytes + drv->name_pool_len;
    size_t data_padded = (used + 3) & ~(size_t)3;
    size_t total = sizeof(BinFileHdr) + sizeof(BinDriveHdr) + data_padded;

    char *buf = ncd_malloc(total);
    size_t pos = 0;

    /* ---- BinFileHdr (drive_count == 1) ---- */
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = NCD_BIN_MAGIC;
    hdr.version     = NCD_BIN_VERSION;
    hdr.show_hidden = db->default_show_hidden ? 1 : 0;
    hdr.show_system = db->default_show_system ? 1 : 0;
    hdr.last_scan   = (int64_t)db->last_scan;
    hdr.drive_count = 1;
    
    /* Preserve user_skipped_update flag from existing file if present */
    FILE *existing = fopen(path, "rb");
    if (existing) {
        uint8_t sig[4];
        if (fread(sig, 1, 4, existing) == 4 &&
            sig[0] == 0x4E && sig[1] == 0x43 &&
            sig[2] == 0x44 && sig[3] == 0x42) {
            /* Valid binary file - read skip flag at offset 16 */
            uint8_t skip_flag = 0;
            fseek(existing, 4 + 2 + 1 + 1 + 8 + 4, SEEK_SET); /* magic+version+hidden+system+last_scan+drive_count */
            fread(&skip_flag, 1, 1, existing);
            hdr.user_skipped_update = skip_flag;
        }
        fclose(existing);
    }
    
    memcpy(buf + pos, &hdr, sizeof(hdr));
    pos += sizeof(hdr);

    /* ---- BinDriveHdr ---- */
    BinDriveHdr dhdr;
    memset(&dhdr, 0, sizeof(dhdr));
    dhdr.letter    = (uint8_t)drv->letter;
    dhdr.type      = drv->type;
    /* copy label safely via platform helper to respect platform semantics */
    platform_strncpy_s(dhdr.label, sizeof(dhdr.label), drv->label);
    dhdr.dir_count = (uint32_t)drv->dir_count;
    dhdr.pool_size = (uint32_t)drv->name_pool_len;
    memcpy(buf + pos, &dhdr, sizeof(dhdr));
    pos += sizeof(dhdr);

    /* ---- DirEntry[] + name_pool + padding ---- */
    memcpy(buf + pos, drv->dirs, dirs_bytes);
    pos += dirs_bytes;
    memcpy(buf + pos, drv->name_pool, drv->name_pool_len);
    pos += drv->name_pool_len;
    size_t padded = (pos + 3) & ~(size_t)3;
    while (pos < padded) buf[pos++] = 0;

    /* ---- atomic write (.tmp -> rename) ---- */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(buf); return false; }

    size_t written = fwrite(buf, 1, pos, f);
    fflush(f);  /* Ensure data is written to OS before closing */
    fclose(f);
    free(buf);

    if (written != pos) { platform_delete_file(tmp_path); return false; }

    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    if (platform_file_exists(path)) {
        if (!platform_move_file(path, old_path)) {
            /* If we can't backup the old file, try to delete it */
            platform_delete_file(path);
        }
    }
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);
        return false;
    }
    return true;
}

/* ================================================================ group database */

char *db_group_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NULL;
    
#if NCD_PLATFORM_WINDOWS
    char local[MAX_PATH] = {0};
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return NULL;
    
    size_t len = strlen(local);
    bool has_sep = (len > 0 && (local[len - 1] == '\\' || local[len - 1] == '/'));
    snprintf(buf, buf_size, "%s%sNCD\\ncd.groups", local, has_sep ? "" : "\\");
#else
    char base[MAX_PATH] = {0};
    if (!platform_get_env("XDG_DATA_HOME", base, sizeof(base)) || !base[0]) {
        char home[MAX_PATH] = {0};
        if (!platform_get_env("HOME", home, sizeof(home)) || !home[0])
            return NULL;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    snprintf(buf, buf_size, "%s/ncd/ncd.groups", base);
#endif
    return buf;
}

NcdGroupDb *db_group_create(void)
{
    NcdGroupDb *gdb = ncd_malloc(sizeof(NcdGroupDb));
    gdb->groups = NULL;
    gdb->count = 0;
    gdb->capacity = 0;
    return gdb;
}

void db_group_free(NcdGroupDb *gdb)
{
    if (!gdb) return;
    free(gdb->groups);
    free(gdb);
}

NcdGroupDb *db_group_load(void)
{
    char path[MAX_PATH];
    if (!db_group_path(path, sizeof(path))) return NULL;
    
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    /* Read and verify header */
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        return NULL;
    }
    
    if (magic != NCD_GROUP_MAGIC || version != NCD_GROUP_VERSION) {
        fclose(f);
        return NULL;
    }
    
    NcdGroupDb *gdb = db_group_create();
    if (count == 0) {
        fclose(f);
        return gdb;
    }
    
    /* Allocate space for groups */
    gdb->capacity = (int)count;
    gdb->groups = ncd_malloc(sizeof(NcdGroupEntry) * count);
    
    /* Read groups */
    for (uint32_t i = 0; i < count; i++) {
        NcdGroupEntry *entry = &gdb->groups[i];
        
        /* Read name length and name */
        uint16_t name_len = 0;
        if (fread(&name_len, sizeof(name_len), 1, f) != 1) goto error;
        if (name_len >= NCD_MAX_GROUP_NAME) goto error;
        if (fread(entry->name, 1, name_len, f) != name_len) goto error;
        entry->name[name_len] = '\0';
        
        /* Read path length and path */
        uint16_t path_len = 0;
        if (fread(&path_len, sizeof(path_len), 1, f) != 1) goto error;
        if (path_len >= NCD_MAX_PATH) goto error;
        if (fread(entry->path, 1, path_len, f) != path_len) goto error;
        entry->path[path_len] = '\0';
        
        /* Read creation time */
        if (fread(&entry->created, sizeof(entry->created), 1, f) != 1) goto error;
        
        gdb->count++;
    }
    
    fclose(f);
    return gdb;
    
error:
    db_group_free(gdb);
    fclose(f);
    return NULL;
}

bool db_group_save(const NcdGroupDb *gdb)
{
    if (!gdb) return false;
    
    char path[MAX_PATH];
    if (!db_group_path(path, sizeof(path))) return false;
    
    /* Ensure directory exists */
    char dir_path[MAX_PATH];
    platform_strncpy_s(dir_path, sizeof(dir_path), path);
    char *last_sep = strrchr(dir_path, '\\');
    if (!last_sep) last_sep = strrchr(dir_path, '/');
    if (last_sep) {
        *last_sep = '\0';
#if NCD_PLATFORM_WINDOWS
        CreateDirectoryA(dir_path, NULL);
#else
        mkdir(dir_path, 0755);
#endif
    }
    
    /* Write to temp file */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return false;
    
    /* Write header */
    uint32_t magic = NCD_GROUP_MAGIC;
    uint32_t version = NCD_GROUP_VERSION;
    uint32_t count = (uint32_t)gdb->count;
    
    if (fwrite(&magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        platform_delete_file(tmp_path);
        return false;
    }
    
    /* Write groups */
    for (int i = 0; i < gdb->count; i++) {
        const NcdGroupEntry *entry = &gdb->groups[i];
        
        uint16_t name_len = (uint16_t)strlen(entry->name);
        uint16_t path_len = (uint16_t)strlen(entry->path);
        
        if (fwrite(&name_len, sizeof(name_len), 1, f) != 1 ||
            fwrite(entry->name, 1, name_len, f) != name_len ||
            fwrite(&path_len, sizeof(path_len), 1, f) != 1 ||
            fwrite(entry->path, 1, path_len, f) != path_len ||
            fwrite(&entry->created, sizeof(entry->created), 1, f) != 1) {
            fclose(f);
            platform_delete_file(tmp_path);
            return false;
        }
    }
    
    fclose(f);
    
    /* Atomic rename */
    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    
    if (platform_file_exists(path)) {
        if (!platform_move_file(path, old_path)) {
            platform_delete_file(path);
        }
    }
    
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);
        return false;
    }
    
    return true;
}

bool db_group_set(NcdGroupDb *gdb, const char *name, const char *path)
{
    if (!gdb || !name || !path) return false;
    
    /* Check if group already exists */
    for (int i = 0; i < gdb->count; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0) {
            /* Update existing group */
            platform_strncpy_s(gdb->groups[i].path, sizeof(gdb->groups[i].path), path);
            gdb->groups[i].created = time(NULL);
            return true;
        }
    }
    
    /* Add new group */
    if (gdb->count >= gdb->capacity) {
        int new_cap = gdb->capacity ? gdb->capacity * 2 : 8;
        if (new_cap > NCD_MAX_GROUPS) new_cap = NCD_MAX_GROUPS;
        if (gdb->count >= new_cap) return false;  /* Too many groups */
        
        gdb->groups = ncd_realloc(gdb->groups, sizeof(NcdGroupEntry) * new_cap);
        gdb->capacity = new_cap;
    }
    
    NcdGroupEntry *entry = &gdb->groups[gdb->count++];
    platform_strncpy_s(entry->name, sizeof(entry->name), name);
    platform_strncpy_s(entry->path, sizeof(entry->path), path);
    entry->created = time(NULL);
    
    return true;
}

bool db_group_remove(NcdGroupDb *gdb, const char *name)
{
    if (!gdb || !name) return false;
    
    for (int i = 0; i < gdb->count; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0) {
            /* Remove by shifting remaining elements */
            memmove(&gdb->groups[i], &gdb->groups[i + 1],
                    (size_t)(gdb->count - i - 1) * sizeof(NcdGroupEntry));
            gdb->count--;
            return true;
        }
    }
    
    return false;  /* Group not found */
}

const NcdGroupEntry *db_group_get(const NcdGroupDb *gdb, const char *name)
{
    if (!gdb || !name) return NULL;
    
    for (int i = 0; i < gdb->count; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0) {
            return &gdb->groups[i];
        }
    }
    
    return NULL;
}

void db_group_list(const NcdGroupDb *gdb)
{
    if (!gdb || gdb->count == 0) {
        printf("No groups defined.\n");
        return;
    }
    
    printf("Groups (%d):\n", gdb->count);
    for (int i = 0; i < gdb->count; i++) {
        printf("  %-20s -> %s\n", gdb->groups[i].name, gdb->groups[i].path);
    }
}

/* ============================================================= drive mgmt */

DriveData *db_add_drive(NcdDatabase *db, char letter)
{
    if (db->drive_count >= db->drive_capacity) {
        db->drive_capacity = db->drive_capacity ? db->drive_capacity * 2 : 8;
        db->drives = ncd_realloc(db->drives, sizeof(DriveData) * db->drive_capacity);
    }
    DriveData *drv = &db->drives[db->drive_count++];
    memset(drv, 0, sizeof(DriveData));
    drv->letter = (char)toupper((unsigned char)letter);
    return drv;
}

DriveData *db_find_drive(NcdDatabase *db, char letter)
{
    letter = (char)toupper((unsigned char)letter);
    for (int i = 0; i < db->drive_count; i++)
        if (db->drives[i].letter == letter)
            return &db->drives[i];
    return NULL;
}

/* ============================================================== dir mgmt  */

int db_add_dir(DriveData *drv, const char *name, int32_t parent,
               bool is_hidden, bool is_system)
{
    /* ---- append name to the per-drive string pool ---- */
    size_t nlen = strlen(name) + 1;          /* include NUL terminator */

    if (drv->name_pool_len + nlen > drv->name_pool_cap) {
        drv->name_pool_cap = drv->name_pool_cap ? drv->name_pool_cap * 2 : 4096;
        while (drv->name_pool_len + nlen > drv->name_pool_cap)
            drv->name_pool_cap *= 2;         /* handles unusually long names */
        drv->name_pool = ncd_realloc(drv->name_pool, drv->name_pool_cap);
    }
    uint32_t name_off = (uint32_t)drv->name_pool_len;
    memcpy(drv->name_pool + drv->name_pool_len, name, nlen);
    drv->name_pool_len += nlen;

    /* ---- append DirEntry ---- */
    if (drv->dir_count >= drv->dir_capacity) {
        drv->dir_capacity = drv->dir_capacity ? drv->dir_capacity * 2 : 64;
        drv->dirs = ncd_realloc(drv->dirs,
                             sizeof(DirEntry) * (size_t)drv->dir_capacity);
    }
    int id      = drv->dir_count;
    DirEntry *e = &drv->dirs[id];
    memset(e, 0, sizeof(*e));    /* zero all fields including padding */
    e->parent    = parent;
    e->name_off  = name_off;
    e->is_hidden = is_hidden ? 1 : 0;
    e->is_system = is_system ? 1 : 0;
    drv->dir_count++;
    return id;
}

/* =========================================================== path helper  */

char *db_full_path(const DriveData *drv, int dir_index,
                   char *buf, size_t buf_size)
{
#if NCD_PLATFORM_WINDOWS
    if (dir_index < 0 || dir_index >= drv->dir_count) {
        snprintf(buf, buf_size, "%c:\\", drv->letter);
        return buf;
    }

    /* Walk up the parent chain collecting component pointers. */
    const int MAX_DEPTH = 128;
    const char *parts[128];
    int depth = 0;
    int cur   = dir_index;
    while (cur >= 0 && depth < MAX_DEPTH) {
        parts[depth++] = drv->name_pool + drv->dirs[cur].name_off;
        cur = drv->dirs[cur].parent;
    }

    /* Build "X:\comp[n-1]\...\comp[0]" */
    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, buf_size - pos, "%c:\\", drv->letter);
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        if (i < depth - 1) {
            buf[pos++] = '\\';
            buf[pos]   = '\0';
        }
        size_t part_len = strlen(parts[i]);
        if (pos + part_len >= buf_size) part_len = buf_size - pos - 1;
        memcpy(buf + pos, parts[i], part_len);
        pos += part_len;
        buf[pos] = '\0';
    }
    return buf;

#else /* NCD_PLATFORM_LINUX */
    /*
     * On Linux the mount root path is stored in drv->label.
     * dir_index == -1 means the mount root itself.
     */
    if (dir_index < 0 || dir_index >= drv->dir_count) {
        snprintf(buf, buf_size, "%s", drv->label);
        return buf;
    }

    /* Walk up the parent chain collecting component pointers. */
    const int MAX_DEPTH = 128;
    const char *parts[128];
    int depth = 0;
    int cur   = dir_index;
    while (cur >= 0 && depth < MAX_DEPTH) {
        parts[depth++] = drv->name_pool + drv->dirs[cur].name_off;
        cur = drv->dirs[cur].parent;
    }

    /* Start with the mount root (e.g. "/" or "/home"). */
    size_t pos = 0;
    size_t root_len = strlen(drv->label);
    if (root_len >= buf_size) root_len = buf_size - 1;
    memcpy(buf, drv->label, root_len);
    pos = root_len;
    buf[pos] = '\0';

    /* Remove trailing slash from root so we don't double up. */
    if (pos > 0 && buf[pos - 1] == '/')
        pos--;

    /* Append "/comp[n-1]/.../comp[0]" */
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        buf[pos++] = '/';
        buf[pos]   = '\0';
        size_t part_len = strlen(parts[i]);
        if (pos + part_len >= buf_size) part_len = buf_size - pos - 1;
        memcpy(buf + pos, parts[i], part_len);
        pos += part_len;
        buf[pos] = '\0';
    }
    return buf;
#endif
}

/* ================================================================== load  */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf)  { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

/* --------------------------------------------------------------- mini-Rdr
 *
 * A zero-allocation streaming JSON reader.  Walks the text forward-only,
 * filling our structs directly without building any intermediate tree.
 *
 * Design principles:
 *  - Fields may appear in any order (handles old "id" fields gracefully).
 *  - Unknown keys are skipped via rdr_skip_value.
 *  - r.err is set on any parse failure; callers check it.
 */

typedef struct { 
    const char *p;      /* current position */
    const char *end;    /* end of buffer */
    int err; 
} Rdr;

static bool rdr_eof(Rdr *r) {
    return r->p >= r->end || *r->p == '\0';
}

static void rdr_skip_ws(Rdr *r)
{
    while (isspace((unsigned char)*r->p)) r->p++;
}

/* Consume expected character c (after optional whitespace). */
static void rdr_eat(Rdr *r, char c)
{
    rdr_skip_ws(r);
    if (*r->p == c) r->p++;
    else r->err = 1;
}

/* Read a quoted JSON string into buf with full escape handling. */
static bool rdr_read_str(Rdr *r, char *buf, size_t cap)
{
    rdr_skip_ws(r);
    if (*r->p != '"') { r->err = 1; return false; }
    r->p++;

    char  *out = buf;
    char  *lim = buf + cap - 1;

    while (*r->p && *r->p != '"') {
        char c;
        if (*r->p == '\\') {
            r->p++;
            switch (*r->p) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'u':  /* 4-hex Unicode: skip 4 hex digits, store '?' */
                    { int i; for (i = 0; i < 4 && r->p[1]; i++) r->p++; }
                    c = '?'; break;
                default:   c = *r->p; break;
            }
        } else {
            c = *r->p;
        }
        if (out < lim) *out++ = c;
        r->p++;
    }
    *out = '\0';

    if (*r->p == '"') r->p++;
    else r->err = 1;
    return !r->err;
}

/* Read a signed 64-bit integer. */
static long long rdr_read_ll(Rdr *r)
{
    rdr_skip_ws(r);
    char *end;
    long long v = strtoll(r->p, &end, 10);
    if (end == r->p) r->err = 1;
    else r->p = end;
    return v;
}

/* Read true/false boolean. */
static bool rdr_read_bool(Rdr *r)
{
    rdr_skip_ws(r);
    if (strncmp(r->p, "true",  4) == 0) { r->p += 4; return true;  }
    if (strncmp(r->p, "false", 5) == 0) { r->p += 5; return false; }
    r->err = 1;
    return false;
}

/* Skip any JSON value at the current position (handles nesting). */
static void rdr_skip_value(Rdr *r)
{
    rdr_skip_ws(r);
    if (rdr_eof(r)) { r->err = 1; return; }
    
    char c = *r->p;
    
    if (c == '"') {                       /* string */
        r->p++;
        while (!rdr_eof(r) && *r->p != '"') {
            if (*r->p == '\\' && !rdr_eof(r) && r->p[1]) r->p++;
            r->p++;
        }
        if (*r->p == '"') r->p++;
        else r->err = 1;  /* Unterminated string */
        
    } else if (c == '{' || c == '[') {    /* object or array */
        int depth = 1;
        r->p++;
        while (!rdr_eof(r) && depth > 0) {
            if (*r->p == '"') {           /* skip string to avoid brace confusion */
                r->p++;
                while (!rdr_eof(r) && *r->p != '"') {
                    if (*r->p == '\\' && !rdr_eof(r) && r->p[1]) r->p++;
                    r->p++;
                }
                if (*r->p == '"') r->p++;
            } else if (*r->p == '{' || *r->p == '[') {
                depth++; r->p++;
            } else if (*r->p == '}' || *r->p == ']') {
                depth--; r->p++;
            } else {
                r->p++;
            }
        }
        if (depth != 0) r->err = 1;  /* Unterminated object/array */
        
    } else {                              /* number / bool / null */
        while (!rdr_eof(r) && *r->p != ',' && *r->p != '}' && *r->p != ']')
            r->p++;
    }
}

/*
 * Try to read the next key from an object and consume ':'.
 * Call immediately after '{' or after consuming a comma.
 * Returns true if a key was read; false at '}' or on error.
 */
static bool rdr_next_key(Rdr *r, char *key, size_t cap)
{
    rdr_skip_ws(r);
    if (*r->p == '}' || r->err) return false;
    if (!rdr_read_str(r, key, cap)) return false;
    rdr_eat(r, ':');
    return !r->err;
}

/*
 * After reading a value inside an object or array, consume ',' if present.
 * The caller is responsible for checking '}' / ']' when this returns false.
 */
static bool rdr_eat_comma(Rdr *r)
{
    rdr_skip_ws(r);
    if (*r->p == ',') { r->p++; return true; }
    return false;
}

/* ---- shrink a drive's arrays to exact size after all entries are loaded  */
static void drv_shrink_to_fit(DriveData *drv)
{
    if (drv->dir_count > 0 && drv->dir_count < drv->dir_capacity) {
        drv->dirs = ncd_realloc(drv->dirs,
                             sizeof(DirEntry) * (size_t)drv->dir_count);
        drv->dir_capacity = drv->dir_count;
    }
    if (drv->name_pool_len > 0 && drv->name_pool_len < drv->name_pool_cap) {
        drv->name_pool     = ncd_realloc(drv->name_pool, drv->name_pool_len);
        drv->name_pool_cap = drv->name_pool_len;
    }
}

NcdDatabase *db_load(const char *path)
{
    char *text = read_file(path);
    if (!text) return NULL;

    Rdr r = { text, text + strlen(text), 0 };
    NcdDatabase *db = db_create();
    platform_strncpy_s(db->db_path, NCD_MAX_PATH, path);

    char key[128];

    rdr_eat(&r, '{');

    while (!r.err && rdr_next_key(&r, key, sizeof(key))) {

        /* ------ top-level fields ------ */
        if (strcmp(key, "version") == 0) {
            db->version = (int)rdr_read_ll(&r);

        } else if (strcmp(key, "lastScan") == 0) {
            db->last_scan = (time_t)rdr_read_ll(&r);

        } else if (strcmp(key, "options") == 0) {
            rdr_eat(&r, '{');
            while (!r.err && rdr_next_key(&r, key, sizeof(key))) {
                if      (strcmp(key, "showHidden") == 0)
                    db->default_show_hidden = rdr_read_bool(&r);
                else if (strcmp(key, "showSystem") == 0)
                    db->default_show_system = rdr_read_bool(&r);
                else
                    rdr_skip_value(&r);
                rdr_eat_comma(&r);
            }
            rdr_eat(&r, '}');

        } else if (strcmp(key, "drives") == 0) {
            rdr_eat(&r, '[');
            rdr_skip_ws(&r);

            while (!r.err && *r.p == '{') {
                DriveData *drv = NULL;

                rdr_eat(&r, '{');
                while (!r.err && rdr_next_key(&r, key, sizeof(key))) {

                    if (strcmp(key, "letter") == 0) {
                        char lbuf[8] = {0};
                        rdr_read_str(&r, lbuf, sizeof(lbuf));
                        if (lbuf[0]) drv = db_add_drive(db, lbuf[0]);

                    } else if (strcmp(key, "label") == 0) {
                        char lbuf[64] = {0};
                        rdr_read_str(&r, lbuf, sizeof(lbuf));
                        if (drv) snprintf(drv->label, sizeof(drv->label), "%s", lbuf);

                    } else if (strcmp(key, "type") == 0) {
                        UINT t = (UINT)rdr_read_ll(&r);
                        if (drv) drv->type = t;

                    } else if (strcmp(key, "dirs") == 0) {
                        rdr_eat(&r, '[');
                        rdr_skip_ws(&r);

                        while (!r.err && drv && *r.p == '{') {
                            int32_t parent   = -1;
                            char    name[NCD_MAX_NAME];
                            uint8_t h        = 0;
                            uint8_t s        = 0;
                            bool    got_name = false;
                            name[0] = '\0';

                            rdr_eat(&r, '{');
                            while (!r.err && rdr_next_key(&r, key, sizeof(key))) {
                                /* "id" is silently skipped (backward compat) */
                                if      (strcmp(key, "parent") == 0)
                                    parent = (int32_t)rdr_read_ll(&r);
                                else if (strcmp(key, "name") == 0) {
                                    rdr_read_str(&r, name, sizeof(name));
                                    got_name = true;
                                }
                                else if (strcmp(key, "h") == 0)
                                    h = (uint8_t)rdr_read_ll(&r);
                                else if (strcmp(key, "s") == 0)
                                    s = (uint8_t)rdr_read_ll(&r);
                                else
                                    rdr_skip_value(&r);
                                rdr_eat_comma(&r);
                            }
                            rdr_eat(&r, '}');

                            if (!r.err && got_name)
                                db_add_dir(drv, name, parent, h, s);

                            rdr_skip_ws(&r);
                            if (*r.p == ',') { r.p++; rdr_skip_ws(&r); }
                        }
                        rdr_eat(&r, ']');

                        /* Shrink to exact size -- no wasted capacity */
                        if (drv) drv_shrink_to_fit(drv);

                    } else {
                        rdr_skip_value(&r);   /* unknown drive key */
                    }

                    rdr_eat_comma(&r);
                }
                rdr_eat(&r, '}');

                rdr_skip_ws(&r);
                if (*r.p == ',') { r.p++; rdr_skip_ws(&r); }
            }
            rdr_eat(&r, ']');

        } else {
            rdr_skip_value(&r);               /* unknown root key */
        }

        rdr_eat_comma(&r);
    }
    rdr_eat(&r, '}');

    free(text);

    if (r.err) {
        db_free(db);
        return NULL;
    }
    return db;
}

/* ================================================================== save  */

bool db_save(const NcdDatabase *db, const char *path)
{
    StringBuilder sb;
    sb_init(&sb);

    /* root object open */
    sb_appendf(&sb, "{\"version\":%d,\"lastScan\":%lld,",
               NCD_VERSION, (long long)db->last_scan);

    sb_appendf(&sb, "\"options\":{\"showHidden\":%s,\"showSystem\":%s},",
               db->default_show_hidden ? "true" : "false",
               db->default_show_system ? "true" : "false");

    sb_append(&sb, "\"drives\":[");

    for (int di = 0; di < db->drive_count; di++) {
        const DriveData *drv = &db->drives[di];
        if (di) sb_appendc(&sb, ',');

        sb_appendf(&sb, "{\"letter\":\"%c\",\"label\":", drv->letter);
        sb_append_json_str(&sb, drv->label);
        sb_appendf(&sb, ",\"type\":%u,\"dirs\":[", drv->type);

        for (int ei = 0; ei < drv->dir_count; ei++) {
            const DirEntry *e    = &drv->dirs[ei];
            const char     *name = drv->name_pool + e->name_off;
            if (ei) sb_appendc(&sb, ',');
            sb_appendf(&sb, "{\"parent\":%d,\"name\":", e->parent);
            sb_append_json_str(&sb, name);
            sb_appendf(&sb, ",\"h\":%d,\"s\":%d}", e->is_hidden, e->is_system);
        }

        sb_append(&sb, "]}");
    }

    sb_append(&sb, "]}");   /* close drives array and root object */

    /* Write to .tmp first, then atomic rename */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(sb.buf); return false; }

    size_t written = fwrite(sb.buf, 1, sb.len, f);
    fflush(f);  /* Ensure data is written to OS before closing */
    fclose(f);
    bool ok = (written == sb.len);
    free(sb.buf);

    if (!ok) { platform_delete_file(tmp_path); return false; }

    /* Rotate: current -> .old, .tmp -> current */
    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    if (platform_file_exists(path)) {
        if (!platform_move_file(path, old_path)) {
            /* If we can't backup the old file, try to delete it */
            platform_delete_file(path);
        }
    }
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);             /* restore on failure */
        return false;
    }
    return true;
}

/* ============================================================ binary save */

/*
 * File layout written by db_save_binary():
 *
 *   [BinFileHdr : 24 bytes]
 *   [BinDriveHdr × drive_count : 80 bytes each]
 *   For each drive (same order):
 *     [DirEntry × dir_count : 12 bytes each]  -- 4-byte aligned
 *     [name_pool : pool_size bytes]
 *     [0–3 zero padding bytes to restore 4-byte alignment]
 *
 * All integer fields are in host byte order (little-endian on x86-64).
 */

/* Compile-time sanity checks on the on-disk struct sizes. */
STATIC_ASSERT(sizeof(BinFileHdr)  == 32, "BinFileHdr size changed");
STATIC_ASSERT(sizeof(BinDriveHdr) == 80, "BinDriveHdr size changed");
STATIC_ASSERT(sizeof(DirEntry)    == 12, "DirEntry size changed");

bool db_save_binary(const NcdDatabase *db, const char *path)
{
    /* ---- compute total output size ---- */
    size_t total = sizeof(BinFileHdr)
                 + (size_t)db->drive_count * sizeof(BinDriveHdr);

    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];
        size_t used = (size_t)drv->dir_count * sizeof(DirEntry)
                    + drv->name_pool_len;
        total += (used + 3) & ~(size_t)3;   /* pad to 4-byte boundary */
    }

    char *buf = ncd_malloc(total);
    size_t pos = 0;

    /* ---- BinFileHdr ---- */
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = NCD_BIN_MAGIC;
    hdr.version     = NCD_BIN_VERSION;
    hdr.show_hidden = db->default_show_hidden ? 1 : 0;
    hdr.show_system = db->default_show_system ? 1 : 0;
    hdr.last_scan   = (int64_t)db->last_scan;
    hdr.drive_count = (uint32_t)db->drive_count;
    hdr.checksum    = 0;  /* Will be computed after data is written */
    memcpy(buf + pos, &hdr, sizeof(hdr));
    pos += sizeof(hdr);

    /* ---- BinDriveHdr array ---- */
    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];
        BinDriveHdr dhdr;
        memset(&dhdr, 0, sizeof(dhdr));
        dhdr.letter    = (uint8_t)drv->letter;
        dhdr.type      = drv->type;
        memcpy(dhdr.label, drv->label, sizeof(dhdr.label));
        dhdr.dir_count = (uint32_t)drv->dir_count;
        dhdr.pool_size = (uint32_t)drv->name_pool_len;
        memcpy(buf + pos, &dhdr, sizeof(dhdr));
        pos += sizeof(dhdr);
    }

    /* ---- data sections (DirEntry[] + name_pool per drive) ---- */
    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];

        size_t dirs_bytes = (size_t)drv->dir_count * sizeof(DirEntry);
        memcpy(buf + pos, drv->dirs, dirs_bytes);
        pos += dirs_bytes;

        memcpy(buf + pos, drv->name_pool, drv->name_pool_len);
        pos += drv->name_pool_len;

        /* Zero-pad to next 4-byte boundary */
        size_t padded = (pos + 3) & ~(size_t)3;
        while (pos < padded) buf[pos++] = 0;
    }

    /* ---- compute CRC64 checksum over all data after the header ---- */
    uint64_t checksum = platform_crc64(buf + sizeof(hdr), pos - sizeof(hdr));
    memcpy(buf + offsetof(BinFileHdr, checksum), &checksum, sizeof(checksum));

    /* ---- atomic write (same pattern as db_save) ---- */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(buf); return false; }

    size_t written = fwrite(buf, 1, pos, f);
    fflush(f);  /* Ensure data is written to OS before closing */
    fclose(f);
    free(buf);

    if (written != pos) { platform_delete_file(tmp_path); return false; }

    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    if (platform_file_exists(path)) {
        if (!platform_move_file(path, old_path)) {
            /* If we can't backup the old file, try to delete it */
            platform_delete_file(path);
        }
    }
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);
        return false;
    }
    return true;
}

/* ============================================================ binary load */

/* Validate file header against corruption/overflow */
static bool validate_file_header(const BinFileHdr *hdr, size_t file_size)
{
    if (hdr->magic != NCD_BIN_MAGIC) {
        fprintf(stderr, "NCD: Invalid database magic number\n");
        return false;
    }
    
    if (hdr->version != NCD_BIN_VERSION) {
        fprintf(stderr, "NCD: Unsupported database version %u\n", hdr->version);
        return false;
    }
    
    if (hdr->drive_count == 0 || hdr->drive_count > NCD_MAX_DRIVES) {
        fprintf(stderr, "NCD: Invalid drive count: %u\n", hdr->drive_count);
        return false;
    }
    
    /* Check headers fit in file */
    size_t headers_size = sizeof(BinFileHdr) + 
                          (size_t)hdr->drive_count * sizeof(BinDriveHdr);
    if (headers_size > file_size || headers_size < sizeof(BinFileHdr)) {
        fprintf(stderr, "NCD: Corrupted database headers\n");
        return false;
    }
    
    return true;
}

/* Validate per-drive header */
static bool validate_drive_header(const BinDriveHdr *dhdr, 
                                   size_t remaining_bytes,
                                   uint64_t *total_dirs)
{
    if (dhdr->dir_count > NCD_MAX_DIRS_PER_DRIVE) {
        fprintf(stderr, "NCD: Drive %c has too many directories: %u\n",
                dhdr->letter, dhdr->dir_count);
        return false;
    }
    
    if (dhdr->pool_size > NCD_MAX_POOL_SIZE) {
        fprintf(stderr, "NCD: Drive %c name pool too large: %u\n",
                dhdr->letter, dhdr->pool_size);
        return false;
    }
    
    /* Check for multiplication overflow before calculating used.
     * Only needed on 32-bit systems where SIZE_MAX could be smaller. */
#if SIZE_MAX <= UINT32_MAX
    if ((size_t)dhdr->dir_count > SIZE_MAX / sizeof(DirEntry)) {
        fprintf(stderr, "NCD: Directory count overflow for drive %c\n",
                dhdr->letter);
        return false;
    }
#endif
    
    size_t dirs_bytes = (size_t)dhdr->dir_count * sizeof(DirEntry);
    
    /* Check addition overflow */
    if (dirs_bytes > SIZE_MAX - dhdr->pool_size) {
        fprintf(stderr, "NCD: Size calculation overflow for drive %c\n",
                dhdr->letter);
        return false;
    }
    
    size_t used = dirs_bytes + dhdr->pool_size;
    size_t padded = (used + 3) & ~(size_t)3;
    
    if (padded > remaining_bytes) {
        fprintf(stderr, "NCD: Drive %c data exceeds file bounds\n",
                dhdr->letter);
        return false;
    }
    
    /* Track total directories to prevent memory exhaustion */
    *total_dirs += dhdr->dir_count;
    if (*total_dirs > NCD_MAX_TOTAL_DIRS) {
        fprintf(stderr, "NCD: Total directory count exceeds maximum\n");
        return false;
    }
    
    return true;
}

NcdDatabase *db_load_binary(const char *path)
{
    /* ---- read entire file into one heap buffer ---- */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    /* Get file size safely */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    
    long sz_long = ftell(f);
    if (sz_long < 0 || (unsigned long)sz_long > NCD_MAX_FILE_SIZE) {
        fprintf(stderr, "NCD: Database file too large or unreadable\n");
        fclose(f);
        return NULL;
    }
    
    size_t sz = (size_t)sz_long;
    if (sz < sizeof(BinFileHdr)) {
        fprintf(stderr, "NCD: Database file too small\n");
        fclose(f);
        return NULL;
    }
    
    rewind(f);
    
    char *blob = malloc(sz);
    if (!blob) {
        fclose(f);
        return NULL;
    }
    
    size_t rd = fread(blob, 1, sz, f);
    fclose(f);
    
    if (rd != sz) {
        free(blob);
        return NULL;
    }
    
    /* ---- validate header ---- */
    BinFileHdr hdr;
    memcpy(&hdr, blob, sizeof(hdr));
    
    if (!validate_file_header(&hdr, sz)) {
        free(blob);
        return NULL;
    }
    
    /* ---- verify CRC64 checksum over all data after the header ---- */
    if (hdr.checksum != 0) {
        uint64_t computed_checksum = platform_crc64(blob + sizeof(hdr), sz - sizeof(hdr));
        if (computed_checksum != hdr.checksum) {
            fprintf(stderr, "NCD: Database checksum mismatch (file may be corrupted)\n");
            free(blob);
            return NULL;
        }
    }
    /* If checksum is 0, the file was saved by an old version that didn't compute checksums.
     * We accept it for backward compatibility, but warn. */
    
    /* ---- pre-validate all drive headers before allocating ---- */
    const char *p = blob + sizeof(BinFileHdr);
    const char *dp = blob + sizeof(BinFileHdr)
                          + (size_t)hdr.drive_count * sizeof(BinDriveHdr);
    size_t remaining = sz - (dp - blob);
    uint64_t total_dirs = 0;
    
    for (uint32_t i = 0; i < hdr.drive_count; i++) {
        BinDriveHdr dhdr;
        memcpy(&dhdr, p + i * sizeof(dhdr), sizeof(dhdr));
        
        if (!validate_drive_header(&dhdr, remaining, &total_dirs)) {
            free(blob);
            return NULL;
        }
        
        size_t used = (size_t)dhdr.dir_count * sizeof(DirEntry) + dhdr.pool_size;
        size_t padded = (used + 3) & ~(size_t)3;
        remaining -= padded;
    }
    
    /* ---- build NcdDatabase from the blob ---- */
    NcdDatabase *db = db_create();
    platform_strncpy_s(db->db_path, NCD_MAX_PATH, path);
    db->default_show_hidden = hdr.show_hidden != 0;
    db->default_show_system = hdr.show_system != 0;
    db->last_scan           = (time_t)hdr.last_scan;
    db->blob_buf            = blob;
    db->is_blob             = true;
    
    /* Pre-allocate the DriveData array (separate from the blob). */
    if (hdr.drive_count > 0) {
        db->drives = ncd_malloc(sizeof(DriveData) * hdr.drive_count);
        memset(db->drives, 0, sizeof(DriveData) * hdr.drive_count);
        db->drive_count    = (int)hdr.drive_count;
        db->drive_capacity = (int)hdr.drive_count;
    }
    
    /*
     * Walk the BinDriveHdr array and the interleaved data sections.
     * p  -- cursor through the BinDriveHdr entries
     * dp -- cursor through the data sections (DirEntry[] + pool per drive)
     *
     * The DriveData.dirs and DriveData.name_pool fields are set as pointers
     * directly into the blob.  The blob owns the memory; db_free() releases
     * it via blob_buf when is_blob is set.
     *
     * Casting char* → DirEntry*: valid because malloc() returns maximally
     * aligned memory and all our offsets within the blob are multiples of 4,
     * satisfying DirEntry's 4-byte alignment requirement.
     */
    p  = blob + sizeof(BinFileHdr);
    dp = blob + sizeof(BinFileHdr)
              + (size_t)hdr.drive_count * sizeof(BinDriveHdr);
    
    for (uint32_t i = 0; i < hdr.drive_count; i++) {
        BinDriveHdr dhdr;
        memcpy(&dhdr, p, sizeof(dhdr));
        p += sizeof(dhdr);
        
        DriveData *drv    = &db->drives[i];
        drv->letter       = (char)dhdr.letter;
        drv->type         = dhdr.type;
        memcpy(drv->label, dhdr.label, sizeof(drv->label));
        drv->dir_count    = (int)dhdr.dir_count;
        drv->dir_capacity = (int)dhdr.dir_count;
        drv->name_pool_len = (size_t)dhdr.pool_size;
        drv->name_pool_cap = (size_t)dhdr.pool_size;
        
        /* Point directly into the blob (no copy). */
        drv->dirs      = (DirEntry *)(void *)dp;
        drv->name_pool = (char *)dp + (size_t)dhdr.dir_count * sizeof(DirEntry);
        
        size_t used = (size_t)dhdr.dir_count * sizeof(DirEntry) + dhdr.pool_size;
        size_t padded = (used + 3) & ~(size_t)3;
        dp += padded;
    }
    
    return db;
}

/* ========================================================== auto-detect  */

/*
 * Inspect the first 4 bytes of path to determine the format:
 *   - Starts with NCD_BIN_MAGIC bytes ('N','C','D','B') → binary
 *   - Anything else (e.g. '{')                          → JSON (legacy)
 *
 * If a JSON file is found it is loaded and immediately resaved as binary
 * so that future loads use the faster path.
 * If a binary file with an old version is found, it is deleted to force a rebuild.
 */
NcdDatabase *db_load_auto(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t sig[4] = {0};
    size_t  rd = fread(sig, 1, sizeof(sig), f);
    
    /* Binary magic: 'N'=0x4E, 'C'=0x43, 'D'=0x44, 'B'=0x42 */
    if (rd == 4 &&
        sig[0] == 0x4E && sig[1] == 0x43 &&
        sig[2] == 0x44 && sig[3] == 0x42) {
        /* Check version and skip flag before attempting full load */
        uint16_t version = 0;
        uint8_t skip_flag = 0;
        fseek(f, 4, SEEK_SET);
        fread(&version, 1, sizeof(version), f);
        /* Read skip flag at offset 16 (after magic+version+hidden+system+last_scan+drive_count) */
        fseek(f, 4 + 2 + 1 + 1 + 8 + 4, SEEK_SET);
        fread(&skip_flag, 1, 1, f);
        fclose(f);
        
        if (version != NCD_BIN_VERSION) {
            /* Old version - only delete if user hasn't chosen to skip update */
            if (!skip_flag) {
                platform_delete_file(path);
            }
            return NULL;
        }
        return db_load_binary(path);
    }
    fclose(f);
    if (rd < 1) return NULL;

    /* JSON (legacy) -- load then silently convert for next run */
    NcdDatabase *db = db_load(path);
    if (db) {
        db_save_binary(db, path);   /* best-effort; ignore failure */
    }
    return db;
}

/* ========================================================= version check  */

int db_check_file_version(const char *path)
{
    if (!path || !path[0]) return DB_VERSION_ERROR;
    
    FILE *f = fopen(path, "rb");
    if (!f) return DB_VERSION_ERROR;
    
    uint8_t sig[4];
    size_t rd = fread(sig, 1, 4, f);
    
    if (rd != 4 || sig[0] != 0x4E || sig[1] != 0x43 || 
        sig[2] != 0x44 || sig[3] != 0x42) {
        fclose(f);
        return DB_VERSION_ERROR;
    }
    
    /* Valid binary file - read version and skip flag */
    uint16_t version = 0;
    uint8_t skip_flag = 0;
    
    fseek(f, 4, SEEK_SET);
    fread(&version, 1, sizeof(version), f);
    /* Read skip flag at offset 16 */
    fseek(f, 4 + 2 + 1 + 1 + 8 + 4, SEEK_SET);
    fread(&skip_flag, 1, 1, f);
    fclose(f);
    
    if (version == NCD_BIN_VERSION) {
        return DB_VERSION_OK;
    }
    
    if (skip_flag) {
        return DB_VERSION_SKIPPED;
    }
    
    return DB_VERSION_MISMATCH;
}

int db_check_all_versions(DbVersionInfo *out, int max_entries)
{
    if (!out || max_entries <= 0) return 0;
    
    int count = 0;
    
#if NCD_PLATFORM_WINDOWS
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26 && count < max_entries; i++) {
        char letter = (char)('A' + i);
        char path[MAX_PATH];
        if (!db_drive_path(letter, path, sizeof(path))) continue;
        if (!platform_file_exists(path)) continue;
        
        /* Check if drive is mounted */
        char root[4] = { letter, ':', '\\', '\0' };
        UINT dtype = platform_get_drive_type(root);
        bool is_mounted = (dtype != PLATFORM_DRIVE_NO_ROOT_DIR && 
                          dtype != PLATFORM_DRIVE_UNKNOWN);
        
        /* Read header to check version and skip flag */
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        
        uint8_t sig[4];
        size_t rd = fread(sig, 1, 4, f);
        uint16_t version = 0;
        uint8_t skip_flag = 0;
        
        if (rd == 4 && sig[0] == 0x4E && sig[1] == 0x43 && 
            sig[2] == 0x44 && sig[3] == 0x42) {
            /* Valid binary file - read version and skip flag */
            fseek(f, 4, SEEK_SET);
            fread(&version, 1, sizeof(version), f);
            /* Skip show_hidden (1) + show_system (1) + last_scan (8) + drive_count (4) */
            fseek(f, 4 + 2 + 8 + 4, SEEK_SET);
            fread(&skip_flag, 1, 1, f);
        }
        fclose(f);
        
        /* Only include if version mismatch AND not skipped AND mounted */
        if (version != NCD_BIN_VERSION && !skip_flag && is_mounted) {
            out[count].letter = letter;
            platform_strncpy_s(out[count].path, sizeof(out[count].path), path);
            out[count].version = version;
            out[count].user_skipped_update = skip_flag ? true : false;
            out[count].is_mounted = is_mounted;
            count++;
        }
    }
#else
    /* Linux: enumerate all mount points and check their databases */
    char mnt_points[26][MAX_PATH];
    char mnt_letters[26];
    int nmounts = 0;
    
    FILE *mf = fopen("/proc/mounts", "r");
    if (mf) {
        char line[1024];
        while (fgets(line, sizeof(line), mf) && nmounts < 26) {
            char device[256], mountpoint[MAX_PATH], fstype[64];
            if (sscanf(line, "%255s %4095s %63s", device, mountpoint, fstype) != 3) continue;
            if (strcmp(fstype, "proc") == 0 || strcmp(fstype, "sysfs") == 0 ||
                strcmp(fstype, "tmpfs") == 0 || strcmp(fstype, "devtmpfs") == 0 ||
                strcmp(fstype, "cgroup") == 0 || strcmp(fstype, "cgroup2") == 0 ||
                strncmp(mountpoint, "/proc", 5) == 0 ||
                strncmp(mountpoint, "/sys", 4) == 0 ||
                strncmp(mountpoint, "/dev", 4) == 0 ||
                strncmp(mountpoint, "/run", 4) == 0)
                continue;
            snprintf(mnt_points[nmounts], MAX_PATH, "%s", mountpoint);
            /* Assign a letter/index for the mount */
            char assigned = (char)(nmounts + 1);
            if (mountpoint[0] == '/' && mountpoint[1] == 'm' && 
                mountpoint[2] == 'n' && mountpoint[3] == 't' && 
                mountpoint[4] == '/' && isalpha((unsigned char)mountpoint[5]) &&
                mountpoint[6] == '\0') {
                assigned = (char)toupper((unsigned char)mountpoint[5]);
            }
            mnt_letters[nmounts] = assigned;
            nmounts++;
        }
        fclose(mf);
    }
    
    /* Check each mount's database */
    for (int i = 0; i < nmounts && count < max_entries; i++) {
        char letter = mnt_letters[i];
        char path[MAX_PATH];
        if (!db_drive_path(letter, path, sizeof(path))) continue;
        if (!platform_file_exists(path)) continue;
        
        /* On Linux, assume mounted if we found it in /proc/mounts */
        bool is_mounted = true;
        
        /* Read header */
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        
        uint8_t sig[4];
        size_t rd = fread(sig, 1, 4, f);
        uint16_t version = 0;
        uint8_t skip_flag = 0;
        
        if (rd == 4 && sig[0] == 0x4E && sig[1] == 0x43 && 
            sig[2] == 0x44 && sig[3] == 0x42) {
            fseek(f, 4, SEEK_SET);
            fread(&version, 1, sizeof(version), f);
            fseek(f, 4 + 2 + 8 + 4, SEEK_SET);
            fread(&skip_flag, 1, 1, f);
        }
        fclose(f);
        
        if (version != NCD_BIN_VERSION && !skip_flag && is_mounted) {
            out[count].letter = letter;
            platform_strncpy_s(out[count].path, sizeof(out[count].path), path);
            out[count].version = version;
            out[count].user_skipped_update = skip_flag ? true : false;
            out[count].is_mounted = is_mounted;
            count++;
        }
    }
#endif
    return count;
}

bool db_set_skip_update_flag(const char *path)
{
    if (!path || !path[0]) return false;
    
    /* Try to load existing database to preserve all data */
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size < (long)sizeof(BinFileHdr)) {
        fclose(f);
        return false;
    }
    
    /* Read entire file */
    char *buf = (char *)malloc(file_size);
    if (!buf) {
        fclose(f);
        return false;
    }
    
    size_t rd = fread(buf, 1, file_size, f);
    fclose(f);
    
    if (rd != (size_t)file_size) {
        free(buf);
        return false;
    }
    
    /* Check magic */
    BinFileHdr *hdr = (BinFileHdr *)buf;
    if (hdr->magic != NCD_BIN_MAGIC) {
        free(buf);
        return false;
    }
    
    /* Set the skip flag (at offset 16: after magic(4)+version(2)+hidden(1)+system(1)+last_scan(8)+drive_count(4)) */
    hdr->user_skipped_update = 1;
    
    /* Write back with atomic rename */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    
    f = fopen(tmp_path, "wb");
    if (!f) {
        free(buf);
        return false;
    }
    
    size_t wr = fwrite(buf, 1, file_size, f);
    fflush(f);
    fclose(f);
    free(buf);
    
    if (wr != (size_t)file_size) {
        platform_delete_file(tmp_path);
        return false;
    }
    
    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    if (platform_file_exists(path)) {
        if (!platform_move_file(path, old_path)) {
            platform_delete_file(path);
        }
    }
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);
        return false;
    }
    return true;
}
