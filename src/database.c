/*
 * database.c  --  Load, save, and manipulate the NCD directory database
 *
 * Debug test flags (debug builds only)
 */
#if DEBUG
bool g_test_no_checksum = false;
bool g_test_slow_mode = false;
#endif

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
#include "matcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#if NCD_PLATFORM_LINUX
#include <sys/stat.h>
#include <sys/types.h>
#endif

/* ================================================================ error handling */

/* Thread-local error buffer for database operations */
static _Thread_local char db_last_error[512] = {0};

/* Set an error message with context */
static void db_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(db_last_error, sizeof(db_last_error), fmt, ap);
    va_end(ap);
}

const char *db_get_last_error(void)
{
    return db_last_error[0] ? db_last_error : "Unknown database error";
}

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
    /* Free cached name index if present */
    if (db->name_index) {
        name_index_free((NameIndex *)db->name_index);
    }
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
    if (ncd_platform_db_default_path(buf, buf_size)) return buf;
    return NULL;
}

char *db_drive_path(char letter, char *buf, size_t buf_size)
{
    if (ncd_platform_db_drive_path(letter, buf, buf_size)) return buf;
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
    
    /* Preserve skipped_rescan flag from existing file if present */
    FILE *existing = fopen(path, "rb");
    if (existing) {
        uint32_t magic = 0;
        if (fread(&magic, sizeof(magic), 1, existing) == 1 &&
            magic == NCD_BIN_MAGIC) {
            /* Valid binary file - preserve skip flag from old file */
            uint8_t skip_flag = 0;
            if (fseek(existing, offsetof(BinFileHdr, skipped_rescan), SEEK_SET) == 0) {
                if (fread(&skip_flag, 1, 1, existing) == 1) {
                    hdr.skipped_rescan = skip_flag;
                }
            }
        }
        fclose(existing);
    }
    (void)hdr; /* Silence unused warning if hdr is not used later */
    
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

    /* ---- compute CRC64 checksum over all data after the header ---- */
    uint64_t checksum = platform_crc64(buf + sizeof(hdr), pos - sizeof(hdr));
    memcpy(buf + offsetof(BinFileHdr, checksum), &checksum, sizeof(checksum));

    /* ---- atomic write (.tmp -> rename) ---- */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(buf); return false; }

    size_t written = fwrite(buf, 1, pos, f);
    if (written != pos) {
        db_set_error("Failed to write database: incomplete write");
        fflush(f);
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
    if (fflush(f) != 0) {
        db_set_error("Failed to flush database to disk (disk full?): %s", strerror(errno));
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
    fclose(f);
    free(buf);

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

/* ========================================================= configuration  */

/*
 * Get the path to the configuration file.
 * Returns path in buf, or NULL on failure.
 */
char *db_config_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NULL;
    buf[0] = '\0';
    
#if NCD_PLATFORM_WINDOWS
    char local[MAX_PATH] = {0};
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return NULL;
    
    size_t len = strlen(local);
    bool has_sep = (len > 0 && (local[len - 1] == '\\' || local[len - 1] == '/'));
    snprintf(buf, buf_size, "%s%sncd\\ncd.config", local, has_sep ? "" : "\\");
#else
    char base[MAX_PATH] = {0};
    if (!platform_get_env("XDG_DATA_HOME", base, sizeof(base)) || !base[0]) {
        char home[MAX_PATH] = {0};
        if (!platform_get_env("HOME", home, sizeof(home)) || !home[0])
            return NULL;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    int w = snprintf(buf, buf_size, "%s/ncd/ncd.config", base);
    if (w <= 0 || (size_t)w >= buf_size) return NULL;
#endif
    return buf;
}

/*
 * Initialize config with default values.
 */
void db_config_init_defaults(NcdConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(NcdConfig));
    cfg->magic = NCD_CFG_MAGIC;
    cfg->version = NCD_CFG_VERSION;
    cfg->default_show_hidden = false;
    cfg->default_show_system = false;
    cfg->default_fuzzy_match = false;
    cfg->default_timeout = -1;  /* -1 means not set (use built-in default of 300) */
    cfg->has_defaults = false;
    cfg->service_retry_count = 0;  /* 0 means use default (NCD_DEFAULT_SERVICE_RETRY_COUNT) */
}

/*
 * Check if configuration file exists.
 */
bool db_config_exists(void)
{
    char path[MAX_PATH];
    if (!db_config_path(path, sizeof(path))) return false;
    return platform_file_exists(path);
}

/*
 * Load configuration from disk.
 * Returns true on success, false if file doesn't exist or is corrupt.
 * On failure, fills cfg with default values.
 */
bool db_config_load(NcdConfig *cfg)
{
    if (!cfg) return false;
    
    db_config_init_defaults(cfg);
    
    char path[MAX_PATH];
    if (!db_config_path(path, sizeof(path))) return false;
    
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    
    NcdConfig temp;
    size_t rd = fread(&temp, 1, sizeof(temp), f);
    fclose(f);
    
    if (rd != sizeof(temp)) return false;
    
    /* Validate magic and version */
    if (temp.magic != NCD_CFG_MAGIC) return false;
    if (temp.version != NCD_CFG_VERSION) return false;
    
    *cfg = temp;
    return true;
}

/*
 * Save configuration to disk.
 * Returns true on success.
 */
bool db_config_save(const NcdConfig *cfg)
{
    if (!cfg) return false;
    
    char path[MAX_PATH];
    if (!db_config_path(path, sizeof(path))) return false;
    
    /* Ensure directory exists */
    char dir[MAX_PATH];
    platform_strncpy_s(dir, sizeof(dir), path);
    char *last_sep = strrchr(dir, '\\');
    if (!last_sep) last_sep = strrchr(dir, '/');
    if (last_sep) {
        *last_sep = '\0';
        platform_create_dir(dir);
    }
    
    /* Write with atomic rename pattern */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return false;
    
    size_t wr = fwrite(cfg, 1, sizeof(NcdConfig), f);
    if (fflush(f) != 0) {
        db_set_error("Failed to flush config to disk (disk full?): %s", strerror(errno));
        fclose(f);
        platform_delete_file(tmp_path);
        return false;
    }
    fclose(f);
    
    if (wr != sizeof(NcdConfig)) {
        db_set_error("Failed to write config: incomplete write");
        platform_delete_file(tmp_path);
        return false;
    }
    
    /* Atomic replace */
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
        platform_create_dir(dir_path);
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

/*
 * Check if exact name+path combination already exists in group.
 * Returns index if found, -1 if not found.
 */
static int db_group_find_exact(NcdMetadata *meta, const char *name, const char *path)
{
    if (!meta || !name || !path) return -1;
    
    NcdGroupDb *gdb = &meta->groups;
    
    for (int i = 0; i < gdb->count; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0 &&
            _stricmp(gdb->groups[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Count entries for a given group name.
 */
static int db_group_count_name(NcdMetadata *meta, const char *name)
{
    if (!meta || !name) return 0;
    
    NcdGroupDb *gdb = &meta->groups;
    int count = 0;
    
    for (int i = 0; i < gdb->count; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0) {
            count++;
        }
    }
    return count;
}

bool db_group_set(NcdMetadata *meta, const char *name, const char *path)
{
    if (!meta || !name || !path) return false;
    
    NcdGroupDb *gdb = &meta->groups;
    
    /* Check if exact name+path combination already exists */
    int existing = db_group_find_exact(meta, name, path);
    if (existing >= 0) {
        /* Already in group - update timestamp and return */
        gdb->groups[existing].created = time(NULL);
        meta->groups_dirty = true;
        return true;  /* Caller should print "already in group" message */
    }
    
    /* Check if name exists with different path - we'll add a new entry */
    /* (This is the new multi-directory group behavior) */
    
    /* Add new group entry */
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
    meta->groups_dirty = true;
    
    return true;
}

bool db_group_remove(NcdMetadata *meta, const char *name)
{
    if (!meta || !name) return false;
    
    NcdGroupDb *gdb = &meta->groups;
    bool removed = false;
    
    /* Remove all entries with matching name */
    for (int i = gdb->count - 1; i >= 0; i--) {
        if (_stricmp(gdb->groups[i].name, name) == 0) {
            /* Remove by shifting remaining elements */
            memmove(&gdb->groups[i], &gdb->groups[i + 1],
                    (size_t)(gdb->count - i - 1) * sizeof(NcdGroupEntry));
            gdb->count--;
            meta->groups_dirty = true;
            removed = true;
        }
    }
    
    return removed;
}

bool db_group_remove_path(NcdMetadata *meta, const char *name, const char *path)
{
    if (!meta || !name || !path) return false;
    
    NcdGroupDb *gdb = &meta->groups;
    
    for (int i = 0; i < gdb->count; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0 &&
            _stricmp(gdb->groups[i].path, path) == 0) {
            /* Remove by shifting remaining elements */
            memmove(&gdb->groups[i], &gdb->groups[i + 1],
                    (size_t)(gdb->count - i - 1) * sizeof(NcdGroupEntry));
            gdb->count--;
            meta->groups_dirty = true;
            return true;
        }
    }
    
    return false;  /* Entry not found */
}

const NcdGroupEntry *db_group_get(NcdMetadata *meta, const char *name)
{
    if (!meta || !name) return NULL;
    
    NcdGroupDb *gdb = &meta->groups;
    
    for (int i = 0; i < gdb->count; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0) {
            return &gdb->groups[i];
        }
    }
    
    return NULL;
}

int db_group_get_all(NcdMetadata *meta, const char *name,
                     const NcdGroupEntry **out, int max_out)
{
    if (!meta || !name || !out || max_out <= 0) return 0;
    
    NcdGroupDb *gdb = &meta->groups;
    int count = 0;
    
    for (int i = 0; i < gdb->count && count < max_out; i++) {
        if (_stricmp(gdb->groups[i].name, name) == 0) {
            out[count++] = &gdb->groups[i];
        }
    }
    
    return count;
}

/*
 * Helper to count unique group names.
 */
static int db_group_count_unique(NcdMetadata *meta)
{
    if (!meta || meta->groups.count == 0) return 0;
    
    int unique = 0;
    for (int i = 0; i < meta->groups.count; i++) {
        /* Count if this is the first occurrence of this name */
        bool first = true;
        for (int j = 0; j < i; j++) {
            if (_stricmp(meta->groups.groups[j].name, meta->groups.groups[i].name) == 0) {
                first = false;
                break;
            }
        }
        if (first) unique++;
    }
    return unique;
}

void db_group_list(NcdMetadata *meta)
{
    if (!meta || meta->groups.count == 0) {
        printf("No groups defined.\n");
        return;
    }
    
    int unique = db_group_count_unique(meta);
    printf("Groups (%d unique, %d total entries):\n", unique, meta->groups.count);
    
    /* Track which entries we've printed */
    bool *printed = ncd_calloc(meta->groups.count, sizeof(bool));
    
    for (int i = 0; i < meta->groups.count; i++) {
        if (printed[i]) continue;
        
        const char *name = meta->groups.groups[i].name;
        
        /* Count entries for this group */
        int entry_count = 0;
        for (int j = i; j < meta->groups.count; j++) {
            if (_stricmp(meta->groups.groups[j].name, name) == 0) {
                entry_count++;
            }
        }
        
        /* Print group header */
        if (entry_count == 1) {
            printf("  %s (1 entry)\n", name);
        } else {
            printf("  %s (%d entries)\n", name, entry_count);
        }
        
        /* Print all entries for this group */
        for (int j = i; j < meta->groups.count; j++) {
            if (_stricmp(meta->groups.groups[j].name, name) == 0) {
                printf("    -> %s\n", meta->groups.groups[j].path);
                printed[j] = true;
            }
        }
    }
    
    free(printed);
}

/* ============================================================= dir history */

bool db_dir_history_add(NcdMetadata *meta, const char *path, char drive)
{
    if (!meta || !path) return false;
    
    NcdDirHistory *hist = &meta->dir_history;
    
    /* Check if this path is already at the top - no change needed */
    if (hist->count > 0 && _stricmp(hist->entries[0].path, path) == 0) {
        return true;
    }
    
    /* Check if this path exists elsewhere in the list - remove it */
    int existing_idx = -1;
    for (int i = 0; i < hist->count; i++) {
        if (_stricmp(hist->entries[i].path, path) == 0) {
            existing_idx = i;
            break;
        }
    }
    
    if (existing_idx >= 0) {
        /* Remove existing entry by shifting */
        memmove(&hist->entries[existing_idx], &hist->entries[existing_idx + 1],
                (size_t)(hist->count - existing_idx - 1) * sizeof(NcdDirHistoryEntry));
        hist->count--;
    }
    
    /* If list is full, drop the last entry */
    if (hist->count >= NCD_DIR_HISTORY_MAX) {
        hist->count = NCD_DIR_HISTORY_MAX - 1;
    }
    
    /* Shift all entries down by one */
    memmove(&hist->entries[1], &hist->entries[0],
            (size_t)hist->count * sizeof(NcdDirHistoryEntry));
    
    /* Add new entry at top */
    platform_strncpy_s(hist->entries[0].path, sizeof(hist->entries[0].path), path);
    hist->entries[0].drive = drive;
    hist->entries[0].timestamp = time(NULL);
    hist->count++;
    
    meta->dir_history_dirty = true;
    return true;
}

const NcdDirHistoryEntry *db_dir_history_get(NcdMetadata *meta, int index)
{
    if (!meta || index < 0 || index >= meta->dir_history.count) return NULL;
    return &meta->dir_history.entries[index];
}

void db_dir_history_swap_first_two(NcdMetadata *meta)
{
    if (!meta || meta->dir_history.count < 2) return;
    
    NcdDirHistoryEntry temp;
    memcpy(&temp, &meta->dir_history.entries[0], sizeof(NcdDirHistoryEntry));
    memcpy(&meta->dir_history.entries[0], &meta->dir_history.entries[1], sizeof(NcdDirHistoryEntry));
    memcpy(&meta->dir_history.entries[1], &temp, sizeof(NcdDirHistoryEntry));
    
    meta->dir_history_dirty = true;
}

int db_dir_history_count(NcdMetadata *meta)
{
    if (!meta) return 0;
    return meta->dir_history.count;
}

void db_dir_history_clear(NcdMetadata *meta)
{
    if (!meta) return;
    meta->dir_history.count = 0;
    meta->dir_history_dirty = true;
}

bool db_dir_history_remove(NcdMetadata *meta, int index)
{
    if (!meta || index < 0 || index >= meta->dir_history.count) {
        return false;
    }
    
    /* Shift entries down to remove the specified index */
    memmove(&meta->dir_history.entries[index],
            &meta->dir_history.entries[index + 1],
            (size_t)(meta->dir_history.count - index - 1) * sizeof(NcdDirHistoryEntry));
    meta->dir_history.count--;
    meta->dir_history_dirty = true;
    
    return true;
}

void db_dir_history_print(NcdMetadata *meta)
{
    if (!meta || meta->dir_history.count == 0) {
        printf("Directory history: empty\n");
        return;
    }
    
    printf("Directory history (%d entries, max %d):\n", 
           meta->dir_history.count, NCD_DIR_HISTORY_MAX);
    for (int i = 0; i < meta->dir_history.count; i++) {
        const NcdDirHistoryEntry *e = &meta->dir_history.entries[i];
        printf("  /%-2d  %c:\\  %s\n", i + 1, e->drive, e->path);
    }
    printf("\nUsage:\n");
    printf("  ncd       - ping-pong between last two directories\n");
    printf("  ncd /0    - same as ncd (ping-pong)\n");
    printf("  ncd /h    - browse history (interactive)\n");
    printf("  ncd /hl   - list history\n");
    printf("  ncd /hc   - clear all history\n");
    printf("  ncd /hc3  - remove entry #3 from history\n");
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
    /* Invalidate cached name index since database structure changed */
    db->name_index_generation++;
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
    if (!buf || buf_size == 0) return NULL;
    
#if NCD_PLATFORM_WINDOWS
    if (dir_index < 0 || dir_index >= drv->dir_count) {
        snprintf(buf, buf_size, "%c:\\", drv->letter);
        return buf;
    }
#else
    if (dir_index < 0 || dir_index >= drv->dir_count) {
        snprintf(buf, buf_size, "%s", drv->label);
        return buf;
    }
#endif

    /*
     * First pass: Count depth and detect cycles using a visited bit vector.
     * A cycle exists if we visit the same index twice.
     */
    int depth = 0;
    int cur = dir_index;
    bool has_cycle = false;
    
    /* Bit vector for visited tracking - allocate on stack (8 bytes per 64 entries) */
    int visited_words = (drv->dir_count + 63) / 64;
    if (visited_words < 1) visited_words = 1;
    uint64_t *visited = ncd_malloc_array((size_t)visited_words, sizeof(uint64_t));
    memset(visited, 0, (size_t)visited_words * sizeof(uint64_t));
    
    while (cur >= 0) {
        /* Check if already visited (cycle detection) */
        int word_idx = cur / 64;
        int bit_idx = cur % 64;
        if (visited[word_idx] & ((uint64_t)1 << bit_idx)) {
            has_cycle = true;
            break;
        }
        visited[word_idx] |= ((uint64_t)1 << bit_idx);
        
        depth++;
        if (depth > drv->dir_count) {
            /* Sanity check: depth can't exceed total directories */
            has_cycle = true;
            break;
        }
        cur = drv->dirs[cur].parent;
    }
    
    free(visited);
    
    if (has_cycle) {
        /* Return empty string to indicate error */
        buf[0] = '\0';
        return NULL;
    }
    
    /*
     * Second pass: Collect path components.
     * Use alloca for dynamic array sized by actual depth.
     */
    const char **parts = ncd_malloc_array((size_t)depth, sizeof(char *));
    cur = dir_index;
    int idx = 0;
    while (cur >= 0 && idx < depth) {
        parts[idx++] = drv->name_pool + drv->dirs[cur].name_off;
        cur = drv->dirs[cur].parent;
    }
    
    /* Build the path string */
    size_t pos = 0;
#if NCD_PLATFORM_WINDOWS
    pos += (size_t)snprintf(buf + pos, buf_size - pos, "%c:\\", drv->letter);
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        if (i < depth - 1) {
            buf[pos++] = '\\';
            buf[pos] = '\0';
        }
        size_t part_len = strlen(parts[i]);
        if (pos + part_len >= buf_size) part_len = buf_size - pos - 1;
        memcpy(buf + pos, parts[i], part_len);
        pos += part_len;
        buf[pos] = '\0';
    }
#else /* NCD_PLATFORM_LINUX */
    size_t root_len = strlen(drv->label);
    if (root_len >= buf_size) root_len = buf_size - 1;
    memcpy(buf, drv->label, root_len);
    pos = root_len;
    buf[pos] = '\0';
    
    if (pos > 0 && buf[pos - 1] == '/')
        pos--;
    
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        buf[pos++] = '/';
        buf[pos] = '\0';
        size_t part_len = strlen(parts[i]);
        if (pos + part_len >= buf_size) part_len = buf_size - pos - 1;
        memcpy(buf + pos, parts[i], part_len);
        pos += part_len;
        buf[pos] = '\0';
    }
#endif
    
    free(parts);
    return buf;
}

/* ================================================================== load  */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || (size_t)sz > NCD_MAX_FILE_SIZE) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    if (sz == 0) { fclose(f); return NULL; }
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
    if (written != sb.len) {
        db_set_error("Failed to write groups: incomplete write");
        fflush(f);
        fclose(f);
        free(sb.buf);
        platform_delete_file(tmp_path);
        return false;
    }
    if (fflush(f) != 0) {
        db_set_error("Failed to flush groups to disk (disk full?): %s", strerror(errno));
        fclose(f);
        free(sb.buf);
        platform_delete_file(tmp_path);
        return false;
    }
    fclose(f);
    free(sb.buf);

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
    if (written != pos) {
        db_set_error("Failed to write binary database: incomplete write");
        fflush(f);
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
    if (fflush(f) != 0) {
        db_set_error("Failed to flush binary database to disk (disk full?): %s", strerror(errno));
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
    fclose(f);
    free(buf);

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
#if DEBUG
    if (hdr.checksum != 0 && !g_test_no_checksum) {
#else
    if (hdr.checksum != 0) {
#endif
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

    uint32_t magic = 0;
    size_t  rd = fread(&magic, 1, sizeof(magic), f);

    if (rd == sizeof(magic) && magic == NCD_BIN_MAGIC) {
        /* Check version and skip flag before attempting full load */
        uint16_t version = 0;
        uint8_t skip_flag = 0;
        if (fseek(f, offsetof(BinFileHdr, version), SEEK_SET) == 0) {
            if (fread(&version, 1, sizeof(version), f) != sizeof(version)) {
                version = 0;
            }
        }
        if (fseek(f, offsetof(BinFileHdr, skipped_rescan), SEEK_SET) == 0) {
            if (fread(&skip_flag, 1, 1, f) != 1) {
                skip_flag = 0;
            }
        }
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
    
    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != NCD_BIN_MAGIC) {
        fclose(f);
        return DB_VERSION_ERROR;
    }
    
    /* Valid binary file - read version and skip flag */
    uint16_t version = 0;
    uint8_t skip_flag = 0;
    bool read_ok = true;
    
    if (fseek(f, offsetof(BinFileHdr, version), SEEK_SET) == 0) {
        if (fread(&version, 1, sizeof(version), f) != sizeof(version)) {
            read_ok = false;
        }
    } else {
        read_ok = false;
    }
    if (fseek(f, offsetof(BinFileHdr, skipped_rescan), SEEK_SET) == 0) {
        if (fread(&skip_flag, 1, 1, f) != 1) {
            read_ok = false;
        }
    } else {
        read_ok = false;
    }
    fclose(f);
    
    if (!read_ok) {
        return DB_VERSION_ERROR;
    }
    
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
        
        uint32_t magic = 0;
        size_t rd = fread(&magic, sizeof(magic), 1, f);
        uint16_t version = 0;
        uint8_t skip_flag = 0;

        if (rd == 1 && magic == NCD_BIN_MAGIC) {
            /* Valid binary file - read version and skip flag */
            if (fseek(f, offsetof(BinFileHdr, version), SEEK_SET) == 0) {
                if (fread(&version, 1, sizeof(version), f) != sizeof(version)) {
                    version = 0;
                }
            }
            if (fseek(f, offsetof(BinFileHdr, skipped_rescan), SEEK_SET) == 0) {
                if (fread(&skip_flag, 1, 1, f) != 1) {
                    skip_flag = 0;
                }
            }
        }
        fclose(f);
        
        /* Only include if version mismatch AND not skipped AND mounted */
        if (version != NCD_BIN_VERSION && !skip_flag && is_mounted) {
            out[count].letter = letter;
            platform_strncpy_s(out[count].path, sizeof(out[count].path), path);
            out[count].version = version;
            out[count].skipped_rescan = skip_flag ? true : false;
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
        
        uint32_t magic = 0;
        size_t rd = fread(&magic, sizeof(magic), 1, f);
        uint16_t version = 0;
        uint8_t skip_flag = 0;

        if (rd == 1 && magic == NCD_BIN_MAGIC) {
            if (fseek(f, offsetof(BinFileHdr, version), SEEK_SET) == 0) {
                if (fread(&version, 1, sizeof(version), f) != sizeof(version)) {
                    version = 0;
                }
            }
            if (fseek(f, offsetof(BinFileHdr, skipped_rescan), SEEK_SET) == 0) {
                if (fread(&skip_flag, 1, 1, f) != 1) {
                    skip_flag = 0;
                }
            }
        }
        fclose(f);

        if (version != NCD_BIN_VERSION && !skip_flag && is_mounted) {
            out[count].letter = letter;
            platform_strncpy_s(out[count].path, sizeof(out[count].path), path);
            out[count].version = version;
            out[count].skipped_rescan = skip_flag ? true : false;
            out[count].is_mounted = is_mounted;
            count++;
        }
    }
#endif
    return count;
}

bool db_set_skipped_rescan_flag(const char *path)
{
    if (!path || !path[0]) return false;
    
    /* Try to load existing database to preserve all data */
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    
    /* Get file size */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long file_size = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    
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
    
    /* Set the skip flag */
    hdr->skipped_rescan = 1;
    
    /* Write back with atomic rename */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    
    f = fopen(tmp_path, "wb");
    if (!f) {
        free(buf);
        return false;
    }
    
    size_t wr = fwrite(buf, 1, file_size, f);
    if (fflush(f) != 0) {
        db_set_error("Failed to flush skip flag to disk (disk full?): %s", strerror(errno));
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
    fclose(f);
    free(buf);
    
    if (wr != (size_t)file_size) {
        db_set_error("Failed to write skip flag: incomplete write");
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

/* ============================================================= path helpers  */

/*
 * Get the path to the legacy history file.
 * Used for migration from old format.
 */
static char *db_history_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NULL;
    buf[0] = '\0';
    
#if NCD_PLATFORM_WINDOWS
    char local[MAX_PATH] = {0};
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return NULL;
    
    size_t len = strlen(local);
    bool has_sep = (len > 0 && (local[len - 1] == '\\' || local[len - 1] == '/'));
    snprintf(buf, buf_size, "%s%sncd.history", local, has_sep ? "" : "\\");
#else
    char base[MAX_PATH] = {0};
    if (!platform_get_env("XDG_DATA_HOME", base, sizeof(base)) || !base[0]) {
        char home[MAX_PATH] = {0};
        if (!platform_get_env("HOME", home, sizeof(home)) || !home[0])
            return NULL;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    snprintf(buf, buf_size, "%s/ncd/ncd.history", base);
#endif
    return buf;
}

/*
 * Atomic file write helper.
 * Writes data to a temp file, then atomically renames to target.
 * Creates a .old backup of the previous file if it exists.
 * 
 * Pattern: write to .tmp -> backup existing to .old -> rename .tmp to final
 * 
 * Returns true on success, false on failure.
 */
static bool db_file_write_atomic(const char *path, const void *data, size_t len)
{
    if (!path || !data || len == 0) return false;
    
    /* Ensure directory exists */
    char dir_path[MAX_PATH];
    platform_strncpy_s(dir_path, sizeof(dir_path), path);
    char *last_sep = strrchr(dir_path, '\\');
    if (!last_sep) last_sep = strrchr(dir_path, '/');
    if (last_sep) {
        *last_sep = '\0';
        platform_create_dir(dir_path);
    }
    
    /* Atomic write pattern: .tmp -> .old -> final */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return false;
    
    size_t wr = fwrite(data, 1, len, f);
    if (fflush(f) != 0) {
        db_set_error("Failed to flush file to disk (disk full?): %s", strerror(errno));
        fclose(f);
        platform_delete_file(tmp_path);
        return false;
    }
    fclose(f);
    
    if (wr != len) {
        db_set_error("Failed to write file: incomplete write");
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

/* ============================================================= metadata  */

char *db_metadata_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NULL;
    buf[0] = '\0';
    
#if NCD_PLATFORM_WINDOWS
    char local[MAX_PATH] = {0};
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return NULL;
    
    size_t len = strlen(local);
    bool has_sep = (len > 0 && (local[len - 1] == '\\' || local[len - 1] == '/'));
    snprintf(buf, buf_size, "%s%sNCD\\%s", local, has_sep ? "" : "\\", NCD_META_FILENAME);
#else
    char base[MAX_PATH] = {0};
    if (!platform_get_env("XDG_DATA_HOME", base, sizeof(base)) || !base[0]) {
        char home[MAX_PATH] = {0};
        if (!platform_get_env("HOME", home, sizeof(home)) || !home[0])
            return NULL;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    snprintf(buf, buf_size, "%s/ncd/%s", base, NCD_META_FILENAME);
#endif
    return buf;
}

bool db_metadata_exists(void)
{
    char path[MAX_PATH];
    if (!db_metadata_path(path, sizeof(path))) return false;
    return platform_file_exists(path);
}

/* ============================================================= exclusion list  */

/* Parse a pattern and fill in the exclusion entry fields */
static bool parse_exclusion_pattern(const char *pattern, NcdExclusionEntry *entry)
{
    if (!pattern || !entry) return false;
    
    /* Clear entry */
    memset(entry, 0, sizeof(NcdExclusionEntry));
    
    /* Check for empty pattern or just '*' */
    if (pattern[0] == '\0') return false;
    if (pattern[0] == '*' && pattern[1] == '\0') return false;
    
    platform_strncpy_s(entry->pattern, sizeof(entry->pattern), pattern);
    
    const char *p = pattern;
    
    /* Parse drive letter (Windows only) */
#if NCD_PLATFORM_WINDOWS
    if (p[1] == ':') {
        char d = (char)toupper((unsigned char)p[0]);
        if (d >= 'A' && d <= 'Z') {
            entry->drive = d;
            p += 2;  /* Skip "C:" */
        }
    } else {
        entry->drive = 0;  /* All drives */
    }
#else
    entry->drive = 0;  /* Linux has no drive concept */
#endif
    
    /* Check for root specifier */
    if (p[0] == '\\' || p[0] == '/') {
        entry->match_from_root = true;
        p++;  /* Skip leading separator */
    }
    
    /* Check for parent\child syntax */
    if (strchr(p, '\\') || strchr(p, '/')) {
        entry->has_parent_match = true;
    }
    
    return true;
}

/* Simple wildcard match: supports * (any sequence) and ? (single char) */
static bool wildcard_match(const char *pattern, const char *text)
{
    const char *p = pattern;
    const char *t = text;
    const char *star_p = NULL;
    const char *star_t = NULL;
    
    while (*t) {
        if (*p == '*') {
            star_p = p++;
            star_t = t;
        } else if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*t)) {
            p++;
            t++;
        } else if (star_p) {
            p = star_p + 1;
            t = ++star_t;
        } else {
            return false;
        }
    }
    
    /* Skip trailing stars */
    while (*p == '*') p++;
    
    return *p == '\0';
}

bool db_exclusion_add(NcdMetadata *meta, const char *pattern)
{
    if (!meta || !pattern) return false;
    
    /* Validate and parse pattern */
    NcdExclusionEntry new_entry;
    if (!parse_exclusion_pattern(pattern, &new_entry)) {
        db_set_error("Invalid exclusion pattern: %s", pattern);
        return false;
    }
    
    /* Check for duplicates */
    for (int i = 0; i < meta->exclusions.count; i++) {
        if (_stricmp(meta->exclusions.entries[i].pattern, pattern) == 0) {
            db_set_error("Pattern already exists: %s", pattern);
            return false;
        }
    }
    
    /* Ensure capacity */
    if (meta->exclusions.count >= meta->exclusions.capacity) {
        int new_cap = meta->exclusions.capacity ? meta->exclusions.capacity * 2 : 16;
        meta->exclusions.entries = ncd_realloc(meta->exclusions.entries,
            sizeof(NcdExclusionEntry) * new_cap);
        meta->exclusions.capacity = new_cap;
    }
    
    /* Add entry */
    meta->exclusions.entries[meta->exclusions.count++] = new_entry;
    meta->exclusions_dirty = true;
    
    return true;
}

bool db_exclusion_remove(NcdMetadata *meta, const char *pattern)
{
    if (!meta || !pattern) return false;
    
    /* Find the pattern */
    int idx = -1;
    for (int i = 0; i < meta->exclusions.count; i++) {
        if (_stricmp(meta->exclusions.entries[i].pattern, pattern) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        db_set_error("Pattern not found: %s", pattern);
        return false;
    }
    
    /* Remove by shifting remaining entries */
    if (idx < meta->exclusions.count - 1) {
        memmove(&meta->exclusions.entries[idx],
                &meta->exclusions.entries[idx + 1],
                (size_t)(meta->exclusions.count - idx - 1) * sizeof(NcdExclusionEntry));
    }
    meta->exclusions.count--;
    meta->exclusions_dirty = true;
    
    return true;
}

bool db_exclusion_check(NcdMetadata *meta, char drive_letter, const char *dir_path)
{
    if (!meta || !dir_path) return false;
    
    /* Normalize drive letter */
#if NCD_PLATFORM_WINDOWS
    char drive = drive_letter ? (char)toupper((unsigned char)drive_letter) : 0;
#else
    (void)drive_letter;
    char drive = 0;
#endif
    
    /* Normalize path for matching (use backslash on Windows, forward on Linux) */
    char norm_path[NCD_MAX_PATH];
    platform_strncpy_s(norm_path, sizeof(norm_path), dir_path);
#if NCD_PLATFORM_WINDOWS
    for (char *p = norm_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif
    
    /* Remove leading separator for comparison */
    char *path_to_match = norm_path;
    if (path_to_match[0] == '\\' || path_to_match[0] == '/') {
        path_to_match++;
    }
    
    for (int i = 0; i < meta->exclusions.count; i++) {
        NcdExclusionEntry *entry = &meta->exclusions.entries[i];
        
        /* Check drive match (0 means all drives) */
        if (entry->drive != 0 && entry->drive != drive) {
            continue;
        }
        
        /* Get the pattern path (skip drive letter if present) */
        const char *pattern = entry->pattern;
#if NCD_PLATFORM_WINDOWS
        if (pattern[1] == ':') {
            pattern += 2;
        }
#endif
        
        /* Handle root-only matches */
        if (entry->match_from_root) {
            /* Pattern starts with \ or /, must match from root */
            if (dir_path[0] != '\\' && dir_path[0] != '/') {
                /* Current path is not from root, skip */
            }
            /* Skip leading separator in pattern for matching */
            if (pattern[0] == '\\' || pattern[0] == '/') {
                pattern++;
            }
            
            if (wildcard_match(pattern, path_to_match)) {
                return true;
            }
        } else {
            /* Check if pattern matches this path component or any parent */
            /* First try matching the full remaining path */
            if (wildcard_match(pattern, path_to_match)) {
                return true;
            }
            
            /* For parent\child patterns, check if any path component matches */
            if (entry->has_parent_match) {
                char *sep = path_to_match;
                while ((sep = strchr(sep, '\\')) != NULL) {
                    sep++;  /* Skip the separator */
                    if (wildcard_match(pattern, sep)) {
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

void db_exclusion_print(NcdMetadata *meta)
{
    if (!meta) return;
    
    if (meta->exclusions.count == 0) {
        printf("NCD exclusion list: empty\n");
        return;
    }
    
    printf("NCD exclusion list (%d entries):\n", meta->exclusions.count);
    printf("  %-4s %-10s %s\n", "Num", "Drive", "Pattern");
    printf("  %s\n", "-------------------------------------------");
    
    for (int i = 0; i < meta->exclusions.count; i++) {
        NcdExclusionEntry *entry = &meta->exclusions.entries[i];
        char drive_str[8];
        if (entry->drive == 0) {
            snprintf(drive_str, sizeof(drive_str), "*");
        } else {
            snprintf(drive_str, sizeof(drive_str), "%c:", entry->drive);
        }
        printf("  %-4d %-10s %s\n", i + 1, drive_str, entry->pattern);
    }
}

void db_exclusion_init_defaults(NcdMetadata *meta)
{
    if (!meta) return;
    
    /* Only add defaults if list is empty */
    if (meta->exclusions.count > 0) return;
    
#if NCD_PLATFORM_WINDOWS
    /* $recycle.bin on all drives */
    db_exclusion_add(meta, "*:$recycle.bin");
    /* Windows directory on C: drive only, root only */
    db_exclusion_add(meta, "C:\\Windows");
#endif
}

/* Forward declarations for heuristics hash table functions */
static void heur_hash_free(NcdHeuristicsV2 *heur);
static void heur_hash_rebuild(NcdHeuristicsV2 *heur);

NcdMetadata *db_metadata_create(void)
{
    NcdMetadata *meta = ncd_malloc(sizeof(NcdMetadata));
    memset(meta, 0, sizeof(NcdMetadata));
    
    db_config_init_defaults(&meta->cfg);
    
    meta->groups.groups = NULL;
    meta->groups.count = 0;
    meta->groups.capacity = 0;
    
    meta->heuristics.entries = NULL;
    meta->heuristics.count = 0;
    meta->heuristics.capacity = 0;
    meta->heuristics.hash_buckets = NULL;
    meta->heuristics.hash_bucket_count = 0;
    
    meta->exclusions.entries = NULL;
    meta->exclusions.count = 0;
    meta->exclusions.capacity = 0;
    
    meta->config_dirty = false;
    meta->groups_dirty = false;
    meta->heuristics_dirty = false;
    meta->exclusions_dirty = false;
    
    return meta;
}

void db_metadata_free(NcdMetadata *meta)
{
    if (!meta) return;
    
    if (meta->groups.groups) {
        free(meta->groups.groups);
    }
    
    if (meta->heuristics.hash_buckets) {
        heur_hash_free(&meta->heuristics);
    }
    if (meta->heuristics.entries) {
        free(meta->heuristics.entries);
    }
    
    if (meta->exclusions.entries) {
        free(meta->exclusions.entries);
    }
    
    free(meta);
}

/* Metadata header size derived from struct -- no manual counting */
#define NCD_META_HEADER_SIZE sizeof(MetaFileHdr)

bool db_metadata_save(NcdMetadata *meta)
{
    if (!meta) return false;
    
    char path[MAX_PATH];
    if (!db_metadata_path(path, sizeof(path))) return false;
    
    /* Calculate sizes with overflow checking */
    size_t config_size = sizeof(NcdConfig);
    size_t groups_entries_size = ncd_mul_overflow_check(
        (size_t)meta->groups.count, sizeof(NcdGroupEntry));
    size_t groups_size = sizeof(uint32_t) + groups_entries_size;
    size_t heur_entries_size = ncd_mul_overflow_check(
        (size_t)meta->heuristics.count, sizeof(NcdHeurEntryV2));
    size_t heur_size = sizeof(uint32_t) + heur_entries_size;
    size_t exclusion_entries_size = ncd_mul_overflow_check(
        (size_t)meta->exclusions.count, sizeof(NcdExclusionEntry));
    size_t exclusion_size = sizeof(uint32_t) + exclusion_entries_size;
    size_t dir_hist_size = sizeof(uint32_t) + 
        (size_t)meta->dir_history.count * sizeof(NcdDirHistoryEntry);
    
    size_t section_overhead = 30; /* 5 section headers @ 6 bytes each (id + size + pad) */

    /* Check total size doesn't overflow */
    size_t total_size = sizeof(MetaFileHdr);
    total_size = ncd_add_overflow_check(total_size, section_overhead);
    total_size = ncd_add_overflow_check(total_size, config_size);
    total_size = ncd_add_overflow_check(total_size, groups_size);
    total_size = ncd_add_overflow_check(total_size, heur_size);
    total_size = ncd_add_overflow_check(total_size, exclusion_size);
    total_size = ncd_add_overflow_check(total_size, dir_hist_size);
    
    uint8_t *buf = ncd_malloc(total_size);
    size_t pos = 0;

    /* Write header (checksum filled in after sections are written) */
    MetaFileHdr hdr = {0};
    hdr.magic         = NCD_META_MAGIC;
    hdr.version       = NCD_META_VERSION;
    hdr.section_count = 5;  /* config + groups + heuristics + exclusions + dir_history */
    memcpy(buf, &hdr, sizeof(hdr));
    pos = sizeof(hdr);
    
    /* Write config section */
    uint8_t section_id = NCD_META_SECTION_CONFIG;
    memcpy(buf + pos, &section_id, 1); pos += 1;
    uint32_t config_data_size = (uint32_t)config_size;
    memcpy(buf + pos, &config_data_size, 4); pos += 4;
    uint8_t config_pad = 0;
    memcpy(buf + pos, &config_pad, 1); pos += 1;
    memcpy(buf + pos, &meta->cfg, config_size); pos += config_size;
    
    /* Write groups section */
    section_id = NCD_META_SECTION_GROUPS;
    memcpy(buf + pos, &section_id, 1); pos += 1;
    uint32_t groups_data_size = (uint32_t)groups_size;
    memcpy(buf + pos, &groups_data_size, 4); pos += 4;
    uint8_t groups_pad = 0;
    memcpy(buf + pos, &groups_pad, 1); pos += 1;
    uint32_t groups_count = (uint32_t)meta->groups.count;
    memcpy(buf + pos, &groups_count, 4); pos += 4;
    for (int i = 0; i < meta->groups.count; i++) {
        memcpy(buf + pos, &meta->groups.groups[i], sizeof(NcdGroupEntry));
        pos += sizeof(NcdGroupEntry);
    }
    
    /* Write heuristics section */
    section_id = NCD_META_SECTION_HEURISTICS;
    memcpy(buf + pos, &section_id, 1); pos += 1;
    uint32_t heur_data_size = (uint32_t)heur_size;
    memcpy(buf + pos, &heur_data_size, 4); pos += 4;
    uint8_t heur_pad = 0;
    memcpy(buf + pos, &heur_pad, 1); pos += 1;
    uint32_t heur_count = (uint32_t)meta->heuristics.count;
    memcpy(buf + pos, &heur_count, 4); pos += 4;
    for (int i = 0; i < meta->heuristics.count; i++) {
        memcpy(buf + pos, &meta->heuristics.entries[i], sizeof(NcdHeurEntryV2));
        pos += sizeof(NcdHeurEntryV2);
    }
    
    /* Write exclusions section */
    section_id = NCD_META_SECTION_EXCLUSIONS;
    memcpy(buf + pos, &section_id, 1); pos += 1;
    uint32_t exclusion_data_size = (uint32_t)exclusion_size;
    memcpy(buf + pos, &exclusion_data_size, 4); pos += 4;
    uint8_t exclusion_pad = 0;
    memcpy(buf + pos, &exclusion_pad, 1); pos += 1;
    uint32_t exclusion_count = (uint32_t)meta->exclusions.count;
    memcpy(buf + pos, &exclusion_count, 4); pos += 4;
    for (int i = 0; i < meta->exclusions.count; i++) {
        memcpy(buf + pos, &meta->exclusions.entries[i], sizeof(NcdExclusionEntry));
        pos += sizeof(NcdExclusionEntry);
    }
    
    /* Write directory history section */
    section_id = NCD_META_SECTION_DIR_HISTORY;
    memcpy(buf + pos, &section_id, 1); pos += 1;
    uint32_t dir_hist_data_size = (uint32_t)dir_hist_size;
    memcpy(buf + pos, &dir_hist_data_size, 4); pos += 4;
    uint8_t dir_hist_pad = 0;
    memcpy(buf + pos, &dir_hist_pad, 1); pos += 1;
    uint32_t dir_hist_count = (uint32_t)meta->dir_history.count;
    memcpy(buf + pos, &dir_hist_count, 4); pos += 4;
    for (int i = 0; i < meta->dir_history.count; i++) {
        memcpy(buf + pos, &meta->dir_history.entries[i], sizeof(NcdDirHistoryEntry));
        pos += sizeof(NcdDirHistoryEntry);
    }
    
    /* Compute and write CRC64 over data after the header */
    uint64_t crc = platform_crc64(buf + sizeof(MetaFileHdr), pos - sizeof(MetaFileHdr));
    memcpy(buf + offsetof(MetaFileHdr, checksum), &crc, sizeof(crc));
    
    /* Ensure directory exists */
    char dir_path[MAX_PATH];
    platform_strncpy_s(dir_path, sizeof(dir_path), path);
    char *last_sep = strrchr(dir_path, '\\');
    if (!last_sep) last_sep = strrchr(dir_path, '/');
    if (last_sep) {
        *last_sep = '\0';
        platform_create_dir(dir_path);
    }
    
    /* Atomic write */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    
    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(buf); return false; }
    
    size_t wr = fwrite(buf, 1, pos, f);
    if (fflush(f) != 0) {
        db_set_error("Failed to flush metadata to disk (disk full?): %s", strerror(errno));
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
    fclose(f);
    free(buf);
    
    if (wr != pos) {
        db_set_error("Failed to write metadata: incomplete write");
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
    
    meta->config_dirty = false;
    meta->groups_dirty = false;
    meta->heuristics_dirty = false;
    meta->exclusions_dirty = false;
    meta->dir_history_dirty = false;
    return true;
}

NcdMetadata *db_metadata_load(void)
{
    NcdMetadata *meta = db_metadata_create();
    
    char path[MAX_PATH];
    if (!db_metadata_path(path, sizeof(path))) {
        /* Try to migrate from legacy files */
        db_metadata_migrate();
        if (!db_metadata_path(path, sizeof(path))) return meta;
    }
    
    /* Check if we need to migrate from legacy files */
    if (!platform_file_exists(path)) {
        db_metadata_migrate();
    }
    
    if (!platform_file_exists(path)) {
        /* No metadata file and no legacy files - return empty metadata with defaults */
        db_exclusion_init_defaults(meta);
        return meta;
    }
    
    platform_strncpy_s(meta->file_path, sizeof(meta->file_path), path);
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* Try to load from legacy config as fallback */
        db_config_load(&meta->cfg);
        return meta;
    }
    
    /* Read header */
    MetaFileHdr hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        db_config_load(&meta->cfg);
        return meta;
    }

    if (hdr.magic != NCD_META_MAGIC || hdr.version != NCD_META_VERSION) {
        fclose(f);
        /* Try legacy migration */
        db_metadata_migrate();
        db_config_load(&meta->cfg);
        return meta;
    }
    
    /* Get file size for CRC verification */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        db_config_load(&meta->cfg);
        return meta;
    }
    long file_size = ftell(f);
    if (fseek(f, NCD_META_HEADER_SIZE, SEEK_SET) != 0) { /* Skip header */
        fclose(f);
        db_config_load(&meta->cfg);
        return meta;
    }
    
    size_t data_size = file_size - NCD_META_HEADER_SIZE;
    uint8_t *data = malloc(data_size);
    if (!data) {
        fclose(f);
        return meta;
    }
    
    if (fread(data, 1, data_size, f) != data_size) {
        free(data);
        fclose(f);
        db_config_load(&meta->cfg);
        return meta;
    }
    fclose(f);
    
    /* Verify CRC64 over data after header */
    uint64_t computed_crc = platform_crc64(data, data_size);
    
    if (computed_crc != hdr.checksum) {
        /* CRC mismatch - try legacy files */
        free(data);
        db_metadata_migrate();
        db_config_load(&meta->cfg);
        return meta;
    }
    
    /* Parse sections */
    size_t pos = 0;
    for (int s = 0; s < hdr.section_count && pos < data_size; s++) {
        if (pos + 6 > data_size) break;
        
        uint8_t section_id = data[pos++];
        uint32_t section_size;
        memcpy(&section_size, data + pos, 4); pos += 4;
        pos++; /* skip padding */
        
        if (pos + section_size > data_size) break;
        
        switch (section_id) {
            case NCD_META_SECTION_CONFIG:
                if (section_size >= sizeof(NcdConfig)) {
                    memcpy(&meta->cfg, data + pos, sizeof(NcdConfig));
                }
                break;
            
            case NCD_META_SECTION_GROUPS:
                if (section_size >= 4) {
                    uint32_t count;
                    memcpy(&count, data + pos, 4);
                    if (count > 0 && count <= NCD_MAX_GROUPS) {
                        meta->groups.capacity = (int)count;
                        meta->groups.groups = ncd_malloc_array(
                            count, sizeof(NcdGroupEntry));
                        size_t entry_offset = pos + 4;
                        for (uint32_t i = 0; i < count && i < NCD_MAX_GROUPS; i++) {
                            if (entry_offset + (i + 1) * sizeof(NcdGroupEntry) <= pos + section_size) {
                                memcpy(&meta->groups.groups[i], 
                                       data + entry_offset + i * sizeof(NcdGroupEntry),
                                       sizeof(NcdGroupEntry));
                                meta->groups.count++;
                            }
                        }
                    }
                }
                break;
                
            case NCD_META_SECTION_HEURISTICS:
                if (section_size >= 4) {
                    uint32_t count;
                    memcpy(&count, data + pos, 4);
                    if (count > 0 && count <= NCD_HEUR_MAX_ENTRIES) {
                        meta->heuristics.capacity = (int)count;
                        meta->heuristics.entries = ncd_malloc_array(
                            count, sizeof(NcdHeurEntryV2));
                        size_t entry_offset = pos + 4;
                        for (uint32_t i = 0; i < count && i < NCD_HEUR_MAX_ENTRIES; i++) {
                            if (entry_offset + (i + 1) * sizeof(NcdHeurEntryV2) <= pos + section_size) {
                                memcpy(&meta->heuristics.entries[i], 
                                       data + entry_offset + i * sizeof(NcdHeurEntryV2),
                                       sizeof(NcdHeurEntryV2));
                                meta->heuristics.count++;
                            }
                        }
                        /* Build hash table for fast lookups */
                        heur_hash_rebuild(&meta->heuristics);
                    }
                }
                break;
                
            case NCD_META_SECTION_EXCLUSIONS:
                if (section_size >= 4) {
                    uint32_t count;
                    memcpy(&count, data + pos, 4);
                    if (count > 0) {
                        meta->exclusions.capacity = (int)count;
                        meta->exclusions.entries = ncd_malloc_array(
                            count, sizeof(NcdExclusionEntry));
                        size_t entry_offset = pos + 4;
                        for (uint32_t i = 0; i < count; i++) {
                            if (entry_offset + (i + 1) * sizeof(NcdExclusionEntry) <= pos + section_size) {
                                memcpy(&meta->exclusions.entries[i], 
                                       data + entry_offset + i * sizeof(NcdExclusionEntry),
                                       sizeof(NcdExclusionEntry));
                                meta->exclusions.count++;
                            }
                        }
                    }
                }
                break;
                
            case NCD_META_SECTION_DIR_HISTORY:
                if (section_size >= 4) {
                    uint32_t count;
                    memcpy(&count, data + pos, 4);
                    if (count > 0 && count <= NCD_DIR_HISTORY_MAX) {
                        size_t entry_offset = pos + 4;
                        for (uint32_t i = 0; i < count && i < NCD_DIR_HISTORY_MAX; i++) {
                            if (entry_offset + (i + 1) * sizeof(NcdDirHistoryEntry) <= pos + section_size) {
                                memcpy(&meta->dir_history.entries[i], 
                                       data + entry_offset + i * sizeof(NcdDirHistoryEntry),
                                       sizeof(NcdDirHistoryEntry));
                                meta->dir_history.count++;
                            }
                        }
                    }
                }
                break;
        }
        
        pos += section_size;
    }
    
    free(data);
    return meta;
}

/* Legacy heuristics entry (for migration) */
typedef struct {
    char search[NCD_MAX_PATH];
    char target[NCD_MAX_PATH];
} NcdHeurEntryOld;

bool db_metadata_migrate(void)
{
    if (db_metadata_exists()) return true;  /* Already migrated */
    
    NcdMetadata *meta = db_metadata_create();
    bool any_migrated = false;
    
    /* Migrate config */
    NcdConfig cfg;
    if (db_config_load(&cfg)) {
        meta->cfg = cfg;
        meta->config_dirty = true;
        any_migrated = true;
    }
    
    /* Migrate groups from legacy ncd.groups file */
    NcdGroupDb *gdb = db_group_load();
    if (gdb && gdb->count > 0) {
        meta->groups.capacity = gdb->count;
        meta->groups.groups = ncd_malloc_array(gdb->count, sizeof(NcdGroupEntry));
        for (int i = 0; i < gdb->count && i < NCD_MAX_GROUPS; i++) {
            memcpy(&meta->groups.groups[i], &gdb->groups[i], sizeof(NcdGroupEntry));
            meta->groups.count++;
        }
        meta->groups_dirty = true;
        any_migrated = true;
        db_group_free(gdb);
    }
    
    /* Migrate heuristics from legacy history file */
    char hist_path[MAX_PATH];
    if (!db_history_path(hist_path, sizeof(hist_path))) {
        /* No history path available - skip migration */
    }
    
    FILE *f = (hist_path[0]) ? fopen(hist_path, "r") : NULL;
    if (f) {
        char line[(NCD_MAX_PATH * 2) + 32];
        while (fgets(line, sizeof(line), f)) {
            if (meta->heuristics.count >= NCD_HEUR_MAX_ENTRIES) break;
            
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '\0') continue;
            
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = '\0';
            const char *search = line;
            const char *target = tab + 1;
            if (search[0] == '\0' || target[0] == '\0') continue;
            
            /* Normalize and add */
            char norm_search[NCD_MAX_PATH];
            char norm_target[NCD_MAX_PATH];
            
            /* Simple lowercase normalization for search */
            size_t i;
            for (i = 0; i < NCD_MAX_PATH - 1 && search[i]; i++) {
                norm_search[i] = (char)tolower((unsigned char)search[i]);
            }
            norm_search[i] = '\0';
            platform_strncpy_s(norm_target, sizeof(norm_target), target);
            
            /* 
             * Check for duplicates (O(n) scan, but n <= 100 during migration).
             * This is acceptable for a one-time migration from legacy format.
             */
            bool found = false;
            for (int j = 0; j < meta->heuristics.count; j++) {
                if (_stricmp(meta->heuristics.entries[j].search, norm_search) == 0) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                if (meta->heuristics.count >= meta->heuristics.capacity) {
                    int new_cap = meta->heuristics.capacity ? meta->heuristics.capacity * 2 : 16;
                    if (new_cap > NCD_HEUR_MAX_ENTRIES) new_cap = NCD_HEUR_MAX_ENTRIES;
                    meta->heuristics.entries = ncd_realloc(meta->heuristics.entries,
                        sizeof(NcdHeurEntryV2) * new_cap);
                    meta->heuristics.capacity = new_cap;
                }
                
                NcdHeurEntryV2 *entry = &meta->heuristics.entries[meta->heuristics.count++];
                memset(entry, 0, sizeof(NcdHeurEntryV2));
                platform_strncpy_s(entry->search, sizeof(entry->search), norm_search);
                platform_strncpy_s(entry->target, sizeof(entry->target), norm_target);
                entry->frequency = 1;
                entry->last_used = time(NULL);
            }
            
            any_migrated = true;
        }
        fclose(f);
    }
    
    if (any_migrated) {
        /* Build hash table after migration before saving */
        if (meta->heuristics.count > 0) {
            heur_hash_rebuild(&meta->heuristics);
        }
        db_metadata_save(meta);
    }
    
    db_metadata_free(meta);
    return any_migrated;
}

int db_metadata_cleanup_legacy(void)
{
    int count = 0;
    
    /* Delete ncd.config */
    char path[MAX_PATH];
    if (db_config_path(path, sizeof(path)) && platform_file_exists(path)) {
        if (platform_delete_file(path)) count++;
    }
    
    /* Delete ncd.history */
    if (db_history_path(path, sizeof(path)) && platform_file_exists(path)) {
        if (platform_delete_file(path)) count++;
    }
    
    /* Delete ncd.groups */
    if (db_group_path(path, sizeof(path)) && platform_file_exists(path)) {
        if (platform_delete_file(path)) count++;
    }
    
    return count;
}

/* ============================================================= enhanced heuristics  */

/* FNV-1a hash for heuristics hash table */
static uint32_t heur_hash_str(const char *s)
{
    uint32_t hash = 2166136261U;
    while (*s) {
        hash ^= (uint32_t)(unsigned char)tolower((unsigned char)*s);
        hash *= 16777619U;
        s++;
    }
    return hash;
}

/* Initialize hash table for heuristics */
static void heur_hash_init(NcdHeuristicsV2 *heur)
{
    heur->hash_bucket_count = 64;  /* Start with 64 buckets (power of 2) */
    heur->hash_buckets = ncd_calloc(heur->hash_bucket_count, sizeof(HeurHashBucket *));
}

/* Free all hash buckets */
static void heur_hash_free(NcdHeuristicsV2 *heur)
{
    if (!heur->hash_buckets) return;
    
    for (int i = 0; i < heur->hash_bucket_count; i++) {
        HeurHashBucket *bucket = heur->hash_buckets[i];
        while (bucket) {
            HeurHashBucket *next = bucket->next;
            free(bucket);
            bucket = next;
        }
    }
    free(heur->hash_buckets);
    heur->hash_buckets = NULL;
    heur->hash_bucket_count = 0;
}

/* Insert entry index into hash table */
static void heur_hash_insert(NcdHeuristicsV2 *heur, const char *search, int entry_idx)
{
    if (!heur->hash_buckets) heur_hash_init(heur);
    
    uint32_t hash = heur_hash_str(search);
    int bucket_idx = (int)(hash & (uint32_t)(heur->hash_bucket_count - 1));
    
    HeurHashBucket *bucket = ncd_malloc(sizeof(HeurHashBucket));
    bucket->entry_idx = entry_idx;
    bucket->next = heur->hash_buckets[bucket_idx];
    heur->hash_buckets[bucket_idx] = bucket;
}

/* Rebuild entire hash table (used after loading from disk) */
static void heur_hash_rebuild(NcdHeuristicsV2 *heur)
{
    if (heur->hash_buckets) {
        heur_hash_free(heur);
    }
    heur_hash_init(heur);
    
    for (int i = 0; i < heur->count; i++) {
        heur_hash_insert(heur, heur->entries[i].search, i);
    }
}

/* Find entry index by search term using hash table */
static int heur_hash_find(NcdHeuristicsV2 *heur, const char *search)
{
    if (!heur->hash_buckets || heur->count == 0) return -1;
    
    uint32_t hash = heur_hash_str(search);
    int bucket_idx = (int)(hash & (uint32_t)(heur->hash_bucket_count - 1));
    
    HeurHashBucket *bucket = heur->hash_buckets[bucket_idx];
    while (bucket) {
        if (_stricmp(heur->entries[bucket->entry_idx].search, search) == 0) {
            return bucket->entry_idx;
        }
        bucket = bucket->next;
    }
    return -1;
}

int db_heur_calculate_score(const NcdHeurEntryV2 *entry, time_t now)
{
    if (!entry) return 0;
    
    double days_since_use = difftime(now, entry->last_used) / (24.0 * 3600.0);
    if (days_since_use < 0) days_since_use = 0;
    
    /* Score formula: (frequency * 10) / (1 + (days_since_last_use / 7)) */
    double score = (entry->frequency * 10.0) / (1.0 + (days_since_use / 7.0));
    
    return (int)(score + 0.5);  /* Round to nearest integer */
}

int db_heur_find(NcdMetadata *meta, const char *search)
{
    if (!meta || !search || !search[0]) return -1;
    
    char norm_search[NCD_MAX_PATH];
    size_t i;
    for (i = 0; i < NCD_MAX_PATH - 1 && search[i]; i++) {
        norm_search[i] = (char)tolower((unsigned char)search[i]);
    }
    norm_search[i] = '\0';
    
    /* Use hash table for O(1) lookup instead of linear scan */
    return heur_hash_find(&meta->heuristics, norm_search);
}

/* Partial match penalty: 70% of original score (30% reduction) */
#define HEUR_PARTIAL_MATCH_PENALTY 70

int db_heur_find_best(NcdMetadata *meta, const char *search, bool *exact_match)
{
    if (!meta || !search || !search[0]) return -1;
    
    char norm_search[NCD_MAX_PATH];
    size_t i;
    for (i = 0; i < NCD_MAX_PATH - 1 && search[i]; i++) {
        norm_search[i] = (char)tolower((unsigned char)search[i]);
    }
    norm_search[i] = '\0';
    
    /* Pre-compute search length once to avoid O(n^2) strlen calls */
    size_t search_len = strlen(norm_search);
    
    int best_idx = -1;
    int best_score = -1;
    time_t now = time(NULL);
    bool exact = false;
    
    for (int j = 0; j < meta->heuristics.count; j++) {
        NcdHeurEntryV2 *entry = &meta->heuristics.entries[j];
        
        /* Check for exact match */
        if (_stricmp(entry->search, norm_search) == 0) {
            int score = db_heur_calculate_score(entry, now);
            if (score > best_score) {
                best_score = score;
                best_idx = j;
                exact = true;
            }
            continue;
        }
        
        /* Check for partial match (only if no exact match found yet) */
        if (!exact) {
            /* 
             * Partial matching strategy:
             * 1. Check if search is a prefix of entry (e.g., "down" matches "downloads")
             * 2. Check if entry is a prefix of search (e.g., "downloads" matches "down")
             * 3. Check if either contains the other (substring match)
             */
            size_t entry_len = strlen(entry->search);
            
            bool partial = false;
            if (search_len <= entry_len) {
                if (strncmp(entry->search, norm_search, search_len) == 0) {
                    partial = true;
                }
            } else {
                if (strncmp(norm_search, entry->search, entry_len) == 0) {
                    partial = true;
                }
            }
            
            /* Also check if search contains entry or vice versa */
            if (!partial) {
                if (strstr(entry->search, norm_search) != NULL ||
                    strstr(norm_search, entry->search) != NULL) {
                    partial = true;
                }
            }
            
            if (partial) {
                int score = db_heur_calculate_score(entry, now);
                /* Apply partial match penalty */
                score = score * HEUR_PARTIAL_MATCH_PENALTY / 100;
                if (score > best_score) {
                    best_score = score;
                    best_idx = j;
                }
            }
        }
    }
    
    if (exact_match) *exact_match = exact;
    return best_idx;
}

void db_heur_note_choice(NcdMetadata *meta, const char *search, const char *target)
{
    if (!meta || !search || !target) return;
    
    char norm_search[NCD_MAX_PATH];
    char norm_target[NCD_MAX_PATH];
    
    size_t i;
    for (i = 0; i < NCD_MAX_PATH - 1 && search[i]; i++) {
        norm_search[i] = (char)tolower((unsigned char)search[i]);
    }
    norm_search[i] = '\0';
    platform_strncpy_s(norm_target, sizeof(norm_target), target);
    
    int idx = db_heur_find(meta, norm_search);
    time_t now = time(NULL);
    
    if (idx >= 0) {
        /* Update existing entry */
        NcdHeurEntryV2 *entry = &meta->heuristics.entries[idx];
        
        if (_stricmp(entry->target, norm_target) != 0) {
            /* Different target - update it */
            platform_strncpy_s(entry->target, sizeof(entry->target), norm_target);
        }
        
        entry->frequency++;
        entry->last_used = now;
        
        /* Move to front (MRU ordering within same score) */
        if (idx > 0) {
            NcdHeurEntryV2 tmp = meta->heuristics.entries[idx];
            memmove(&meta->heuristics.entries[1], &meta->heuristics.entries[0],
                    (size_t)idx * sizeof(NcdHeurEntryV2));
            meta->heuristics.entries[0] = tmp;
        }
    } else {
        /* New entry - insert with eviction if at capacity */
        if (meta->heuristics.count >= NCD_HEUR_MAX_ENTRIES) {
            /* 
             * Eviction policy: Remove the lowest-scored entry.
             * 
             * This is O(n) but n <= NCD_HEUR_MAX_ENTRIES (100), so it's acceptable.
             * Alternative: Random eviction would be O(1) but may remove high-value entries.
             * We chose score-based eviction to preserve frequently/recently used entries.
             */
            int min_score = INT_MAX;
            int min_idx = 0;
            for (int i = 0; i < meta->heuristics.count; i++) {
                int score = db_heur_calculate_score(&meta->heuristics.entries[i], now);
                if (score < min_score) {
                    min_score = score;
                    min_idx = i;
                }
            }
            /* Remove min_idx by shifting remaining entries down */
            memmove(&meta->heuristics.entries[min_idx], 
                    &meta->heuristics.entries[min_idx + 1],
                    (size_t)(meta->heuristics.count - min_idx - 1) * sizeof(NcdHeurEntryV2));
            meta->heuristics.count--;
        }
        
        /* Ensure capacity */
        if (meta->heuristics.count >= meta->heuristics.capacity) {
            int new_cap = meta->heuristics.capacity ? meta->heuristics.capacity * 2 : 16;
            if (new_cap > NCD_HEUR_MAX_ENTRIES) new_cap = NCD_HEUR_MAX_ENTRIES;
            meta->heuristics.entries = ncd_realloc(meta->heuristics.entries,
                sizeof(NcdHeurEntryV2) * new_cap);
            meta->heuristics.capacity = new_cap;
        }
        
        /* Insert at front */
        memmove(&meta->heuristics.entries[1], &meta->heuristics.entries[0],
                (size_t)meta->heuristics.count * sizeof(NcdHeurEntryV2));
        
        NcdHeurEntryV2 *entry = &meta->heuristics.entries[0];
        memset(entry, 0, sizeof(NcdHeurEntryV2));
        platform_strncpy_s(entry->search, sizeof(entry->search), norm_search);
        platform_strncpy_s(entry->target, sizeof(entry->target), norm_target);
        entry->frequency = 1;
        entry->last_used = now;
        
        meta->heuristics.count++;
    }
    
    /* Rebuild hash table since entry indices may have changed */
    heur_hash_rebuild(&meta->heuristics);
    
    meta->heuristics_dirty = true;
}

bool db_heur_get_preferred(NcdMetadata *meta, const char *search,
                           char *out_path, size_t out_size)
{
    if (!meta || !search || !out_path || out_size == 0) return false;
    out_path[0] = '\0';
    
    bool exact = false;
    int idx = db_heur_find_best(meta, search, &exact);
    
    if (idx >= 0) {
        /* Only use exact matches or high-scoring partial matches */
        time_t now = time(NULL);
        int score = db_heur_calculate_score(&meta->heuristics.entries[idx], now);
        
        if (exact || score >= 5) {
            platform_strncpy_s(out_path, out_size, meta->heuristics.entries[idx].target);
            return out_path[0] != '\0';
        }
    }
    
    return false;
}

void db_heur_clear(NcdMetadata *meta)
{
    if (!meta) return;
    
    meta->heuristics.count = 0;
    
    /* Clear hash table but keep buckets allocated for reuse */
    if (meta->heuristics.hash_buckets) {
        for (int i = 0; i < meta->heuristics.hash_bucket_count; i++) {
            HeurHashBucket *bucket = meta->heuristics.hash_buckets[i];
            while (bucket) {
                HeurHashBucket *next = bucket->next;
                free(bucket);
                bucket = next;
            }
            meta->heuristics.hash_buckets[i] = NULL;
        }
    }
    
    meta->heuristics_dirty = true;
}

typedef struct {
    int idx;
    int score;
} HeurSortEntry;

static int compare_heur_desc(const void *a, const void *b)
{
    const HeurSortEntry *ha = (const HeurSortEntry *)a;
    const HeurSortEntry *hb = (const HeurSortEntry *)b;
    return hb->score - ha->score;  /* descending */
}

void db_heur_print(NcdMetadata *meta)
{
    if (!meta) return;
    
    if (meta->heuristics.count == 0) {
        printf("NCD history: empty\n");
        return;
    }
    
    /* Sort by score */
    HeurSortEntry *sorted = ncd_malloc(sizeof(HeurSortEntry) * meta->heuristics.count);
    time_t now = time(NULL);
    
    for (int i = 0; i < meta->heuristics.count; i++) {
        sorted[i].idx = i;
        sorted[i].score = db_heur_calculate_score(&meta->heuristics.entries[i], now);
    }
    
    qsort(sorted, (size_t)meta->heuristics.count, sizeof(HeurSortEntry), compare_heur_desc);
    
    printf("NCD history (%d entries, sorted by relevance):\n", meta->heuristics.count);
    printf("  %-4s %-20s %-30s %s\n", "Rank", "Search", "Target", "Score");
    printf("  %s\n", "---------------------------------------------------------------");
    
    for (int i = 0; i < meta->heuristics.count && i < 20; i++) {
        NcdHeurEntryV2 *entry = &meta->heuristics.entries[sorted[i].idx];
        /* Truncate long strings for display */
        char search_disp[21];
        char target_disp[31];
        
        if (strlen(entry->search) > 20) {
            snprintf(search_disp, sizeof(search_disp), "%.17s...", entry->search);
        } else {
            snprintf(search_disp, sizeof(search_disp), "%s", entry->search);
        }
        
        if (strlen(entry->target) > 30) {
            snprintf(target_disp, sizeof(target_disp), "...%.27s", 
                     entry->target + strlen(entry->target) - 27);
        } else {
            snprintf(target_disp, sizeof(target_disp), "%s", entry->target);
        }
        
        printf("  %-4d %-20s %-30s %d\n", i + 1, search_disp, target_disp, sorted[i].score);
    }
    
    free(sorted);
}

/* ============================================================= atomic rescan helpers  */

/*
 * Create a deep copy of a DriveData structure for backup purposes.
 * 
 * This is used by the atomic rescan mechanism to preserve the existing
 * drive data before attempting a rescan. If the rescan fails, the backup
 * can be restored.
 * 
 * The returned backup must be freed with db_drive_backup_free().
 * 
 * Note: The backup stores exact sizes (not capacities) to minimize memory usage.
 */
DriveData *db_drive_backup_create(const DriveData *src)
{
    if (!src) return NULL;
    
    DriveData *backup = ncd_malloc(sizeof(DriveData));
    memcpy(backup, src, sizeof(DriveData));
    
    /* Deep copy dirs array - allocate exact size needed */
    if (src->dir_count > 0 && src->dirs) {
        size_t dirs_bytes = (size_t)src->dir_count * sizeof(DirEntry);
        backup->dirs = ncd_malloc(dirs_bytes);
        memcpy(backup->dirs, src->dirs, dirs_bytes);
    } else {
        backup->dirs = NULL;
    }
    
    /* Deep copy name pool - allocate exact size needed */
    if (src->name_pool_len > 0 && src->name_pool) {
        backup->name_pool = ncd_malloc(src->name_pool_len);
        memcpy(backup->name_pool, src->name_pool, src->name_pool_len);
    } else {
        backup->name_pool = NULL;
    }
    
    /* Store actual sizes, not capacities (backup is immutable) */
    backup->dir_capacity = src->dir_count;
    backup->name_pool_cap = src->name_pool_len;
    
    return backup;
}

void db_drive_backup_free(DriveData *backup)
{
    if (!backup) return;
    
    if (backup->dirs) free(backup->dirs);
    if (backup->name_pool) free(backup->name_pool);
    free(backup);
}

/*
 * Restore a DriveData structure from a backup.
 * 
 * This is used by the atomic rescan mechanism to restore the original
 * drive data when a rescan fails.
 * 
 * Note: This function frees the current data in dst and replaces it with
 * deep copies from the backup. The backup remains valid and must be freed
 * separately by the caller.
 */
void db_drive_restore_from_backup(DriveData *dst, const DriveData *backup)
{
    if (!dst || !backup) return;
    
    /* Free current data in destination */
    if (dst->dirs) free(dst->dirs);
    if (dst->name_pool) free(dst->name_pool);
    
    /* Copy metadata from backup */
    memcpy(dst, backup, sizeof(DriveData));
    
    /* Deep copy dirs from backup */
    if (backup->dir_count > 0 && backup->dirs) {
        size_t dirs_bytes = (size_t)backup->dir_count * sizeof(DirEntry);
        dst->dirs = ncd_malloc(dirs_bytes);
        memcpy(dst->dirs, backup->dirs, dirs_bytes);
    } else {
        dst->dirs = NULL;
    }
    
    /* Deep copy name pool from backup */
    if (backup->name_pool_len > 0 && backup->name_pool) {
        dst->name_pool = ncd_malloc(backup->name_pool_len);
        memcpy(dst->name_pool, backup->name_pool, backup->name_pool_len);
    } else {
        dst->name_pool = NULL;
    }
}
