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
#include "../shared/strbuilder.h"
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
#include <unistd.h>
#endif

/* Debug test flags (debug builds only) */
#if DEBUG
bool g_test_no_checksum = false;
bool g_test_slow_mode = false;
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
    db->ref_count = 1;  /* Start with reference count of 1 */
    return db;
}

NcdDatabase *db_retain(NcdDatabase *db)
{
    if (!db) return NULL;
    db->ref_count++;
    return db;
}

void db_free(NcdDatabase *db)
{
    if (!db) return;
    
    /* Decrement reference count - only free when it reaches zero */
    db->ref_count--;
    if (db->ref_count > 0) {
        return;  /* Still has references, don't free yet */
    }
    
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

void db_make_mutable(NcdDatabase *db)
{
    if (!db || !db->is_blob) return;

    for (int i = 0; i < db->drive_count; i++) {
        DriveData *drv = &db->drives[i];
        if (drv->dir_count > 0 && drv->dirs) {
            size_t dirs_size = sizeof(DirEntry) * (size_t)drv->dir_count;
            DirEntry *new_dirs = (DirEntry *)ncd_malloc(dirs_size);
            memcpy(new_dirs, drv->dirs, dirs_size);
            drv->dirs = new_dirs;
            drv->dir_capacity = drv->dir_count;
        } else {
            drv->dirs = NULL;
            drv->dir_capacity = 0;
        }

        if (drv->name_pool_len > 0 && drv->name_pool) {
            char *new_pool = (char *)ncd_malloc(drv->name_pool_len);
            memcpy(new_pool, drv->name_pool, drv->name_pool_len);
            drv->name_pool = new_pool;
            drv->name_pool_cap = drv->name_pool_len;
        } else {
            drv->name_pool = NULL;
            drv->name_pool_cap = 0;
        }
    }

    db->is_blob = false;
    free(db->blob_buf);
    db->blob_buf = NULL;
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

/* Forward declaration - defined later in file */
extern uint8_t g_db_text_encoding;

bool db_save_binary_single(const NcdDatabase *db, int drive_idx,
                            const char *path)
{
    if (drive_idx < 0 || drive_idx >= db->drive_count) return false;
    const DriveData *drv = &db->drives[drive_idx];

    /* ---- compute data section size (for padding calculation) ---- */
    uint8_t text_encoding = db_get_text_encoding();
    size_t bom_size = (text_encoding == NCD_TEXT_UTF16LE) ? NCD_UTF16LE_BOM_LEN : 0;
    size_t dirs_bytes = (size_t)drv->dir_count * sizeof(DirEntry);
    size_t used       = dirs_bytes + drv->name_pool_len;
    size_t data_padded = (used + 3) & ~(size_t)3;
    
    /* ---- compute total output size (including optional BOM) ---- */
    /* Header + DriveHeader + DataSection */
    size_t header_size = bom_size + sizeof(BinFileHdr) + sizeof(BinDriveHdr);
    size_t total = header_size + data_padded;

    char *buf = ncd_malloc(total);
    size_t pos = 0;

    /* ---- UTF16LE BOM (if UTF16 mode) ---- */
    if (text_encoding == NCD_TEXT_UTF16LE) {
        memcpy(buf + pos, NCD_UTF16LE_BOM, NCD_UTF16LE_BOM_LEN);
        pos += NCD_UTF16LE_BOM_LEN;
    }

    /* ---- BinFileHdr (drive_count == 1) ---- */
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = NCD_BIN_MAGIC;
    hdr.version     = NCD_BIN_VERSION;
    hdr.show_hidden = db->default_show_hidden ? 1 : 0;
    hdr.show_system = db->default_show_system ? 1 : 0;
    hdr.last_scan   = (int64_t)db->last_scan;
    hdr.drive_count = 1;
    hdr.encoding    = text_encoding;
    
    /* Preserve skipped_rescan flag from existing file if present */
    FILE *existing = fopen(path, "rb");
    if (existing) {
        uint32_t magic = 0;
        /* Check for BOM when reading existing file */
        uint8_t bom_check[2];
        size_t bom_read = fread(bom_check, 1, 2, existing);
        bool has_bom = (bom_read == 2 && bom_check[0] == 0xFF && bom_check[1] == 0xFE);
        
        /* Read magic after potential BOM */
        if (has_bom) {
            if (fread(&magic, sizeof(magic), 1, existing) != 1) {
                magic = 0; /* Failed to read, treat as invalid */
            }
        } else {
            rewind(existing);
            if (fread(&magic, sizeof(magic), 1, existing) != 1) {
                magic = 0; /* Failed to read, treat as invalid */
            }
        }
        
        if (magic == NCD_BIN_MAGIC) {
            /* Valid binary file - preserve skip flag from old file */
            uint8_t skip_flag = 0;
            size_t skip_offset = has_bom ? NCD_UTF16LE_BOM_LEN + offsetof(BinFileHdr, skipped_rescan) 
                                          : offsetof(BinFileHdr, skipped_rescan);
#if NCD_PLATFORM_WINDOWS
            if (_fseeki64(existing, (__int64)skip_offset, SEEK_SET) == 0) {
#else
            if (fseeko(existing, (off_t)skip_offset, SEEK_SET) == 0) {
#endif
                if (fread(&skip_flag, 1, 1, existing) == 1) {
                    hdr.skipped_rescan = skip_flag;
                }
            }
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
    /* Pad data section to 4-byte boundary (match data_padded calculation) */
    size_t data_end = bom_size + sizeof(BinFileHdr) + sizeof(BinDriveHdr) + data_padded;
    while (pos < data_end) buf[pos++] = 0;

    /* ---- compute CRC64 checksum over all data after the header ---- */
    uint64_t checksum = platform_crc64(buf + bom_size + sizeof(hdr), pos - bom_size - sizeof(hdr));
    memcpy(buf + bom_size + offsetof(BinFileHdr, checksum), &checksum, sizeof(checksum));

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

    /* Atomic file replacement (replaces destination if it exists) */
    if (!platform_move_file_replace(tmp_path, path)) {
        platform_delete_file(tmp_path);
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
    snprintf(buf, buf_size, "%s%sncd%sncd.config", local, has_sep ? "" : NCD_PATH_SEP, NCD_PATH_SEP);
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
    cfg->has_defaults = true;
    cfg->service_retry_count = 0;  /* 0 means use default (NCD_DEFAULT_SERVICE_RETRY_COUNT) */
    cfg->rescan_interval_hours = NCD_RESCAN_HOURS_DEFAULT;  /* 24 hours default */
    cfg->text_encoding = NCD_TEXT_UTF8;  /* Default to UTF-8 encoding */
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
    
    /* Validate magic */
    if (temp.magic != NCD_CFG_MAGIC) return false;
    
    /* Handle version migration */
    if (temp.version == NCD_CFG_VERSION) {
        /* Current version - use as-is */
        *cfg = temp;
        return true;
    }
    
    /* Migrate from version 3 (no text_encoding field) */
    if (temp.version == 3) {
        temp.text_encoding = NCD_TEXT_UTF8;  /* Default to UTF8 for migrated configs */
        temp.version = NCD_CFG_VERSION;
        *cfg = temp;
        return true;
    }
    
    /* Unsupported version */
    return false;
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
    
    /* Atomic file replacement */
    if (!platform_move_file_replace(tmp_path, path)) {
        platform_delete_file(tmp_path);
        return false;
    }
    return true;
}

/* ================================================================ group database */

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

const NcdDirHistoryEntry *db_dir_history_get(const NcdMetadata *meta, int index)
{
    if (!meta || index < 0 || index >= meta->dir_history.count) return NULL;
    return &meta->dir_history.entries[index];
}

const NcdDirHistoryEntry *db_dir_history_get_by_display_index(const NcdMetadata *meta, int display_index)
{
    if (!meta || display_index < 1 || display_index > meta->dir_history.count) return NULL;
    /* display_index 1 = oldest (entries[count-1]), display_index count = newest (entries[0]) */
    int internal_index = meta->dir_history.count - display_index;
    return &meta->dir_history.entries[internal_index];
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

int db_dir_history_count(const NcdMetadata *meta)
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
    /* Print oldest first (higher index = more recent, so reverse order) */
    for (int i = meta->dir_history.count - 1; i >= 0; i--) {
        const NcdDirHistoryEntry *e = &meta->dir_history.entries[i];
        int display_num = meta->dir_history.count - i;  /* /1 = oldest, /9 = newest */
        printf("  /%-2d  %c:\\  %s\n", display_num, e->drive, e->path);
    }
    printf("\nUsage:\n");
    printf("  ncd       - ping-pong between last two directories\n");
    printf("  ncd -0    - same as ncd (ping-pong)\n");
    printf("  ncd -1    - jump to oldest directory\n");
    printf("  ncd -%d   - jump to most recent directory\n", meta->dir_history.count);
    printf("  ncd -h    - browse history (interactive)\n");
    printf("  ncd -h:l  - list history\n");
    printf("  ncd -h:c  - clear all history\n");
    printf("  ncd -h:c3 - remove entry #3 from history\n");
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
        /* If label looks like a path (contains ':\'), use it as mount point;
         * otherwise fall back to drive root for volume labels. */
        if (drv->label[0] && strstr(drv->label, ":\\")) {
            snprintf(buf, buf_size, "%s", drv->label);
        } else {
            snprintf(buf, buf_size, "%c:%s", drv->letter, NCD_PATH_SEP);
        }
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
     * 
     * PERFORMANCE: Use stack allocation for small visited bitsets (up to 1024 dirs).
     * This avoids heap allocation churn on the hot path.
     */
    int depth = 0;
    int cur = dir_index;
    bool has_cycle = false;
    
    /* Bit vector for visited tracking - use stack for small drives, heap for large */
    int visited_words = (drv->dir_count + 63) / 64;
    if (visited_words < 1) visited_words = 1;
    
    /* Stack allocation for up to 1024 directories (16 x 64-bit words = 128 bytes) */
    uint64_t visited_stack[16];
    uint64_t *visited;
    bool visited_on_stack = (visited_words <= 16);
    
    if (visited_on_stack) {
        visited = visited_stack;
        memset(visited, 0, (size_t)visited_words * sizeof(uint64_t));
    } else {
        visited = ncd_malloc_array((size_t)visited_words, sizeof(uint64_t));
        memset(visited, 0, (size_t)visited_words * sizeof(uint64_t));
    }
    
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
    
    if (!visited_on_stack) {
        free(visited);
    }
    
    if (has_cycle) {
        /* Return empty string to indicate error */
        buf[0] = '\0';
        return NULL;
    }
    
    /*
     * Second pass: Collect path components.
     * PERFORMANCE: Use stack allocation for typical path depths (up to 64 components).
     * Heap allocation only for extremely deep paths.
     */
    #define PATH_STACK_DEPTH 64
    const char *parts_stack[PATH_STACK_DEPTH];
    const char **parts;
    bool parts_on_stack = (depth <= PATH_STACK_DEPTH);
    
    if (!parts_on_stack) {
        parts = ncd_malloc_array((size_t)depth, sizeof(char *));
    } else {
        parts = parts_stack;
    }
    
    cur = dir_index;
    int idx = 0;
    while (cur >= 0 && idx < depth) {
        parts[idx++] = drv->name_pool + drv->dirs[cur].name_off;
        cur = drv->dirs[cur].parent;
    }
    
    /* Build the path string */
    size_t pos = 0;
#if NCD_PLATFORM_WINDOWS
    /* If label looks like a path (contains ':\'), use it as mount point;
     * otherwise fall back to drive root for volume labels. */
    if (drv->label[0] && strstr(drv->label, ":\\")) {
        pos += (size_t)snprintf(buf + pos, buf_size - pos, "%s", drv->label);
    } else {
        pos += (size_t)snprintf(buf + pos, buf_size - pos, "%c:%s", drv->letter, NCD_PATH_SEP);
    }
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        bool need_sep = (i < depth - 1);
        if (!need_sep && pos > 0) {
            char c = buf[pos - 1];
            need_sep = (c != '\\' && c != '/');
        }
        if (need_sep) {
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
    
    if (!parts_on_stack) {
        free(parts);
    }
    return buf;
}

/* ==================================================== exclusion filtering */

int db_filter_excluded(NcdDatabase *db, NcdMetadata *meta)
{
    if (!db || !meta || meta->exclusions.count == 0) return 0;
    
    if (db->is_blob) db_make_mutable(db);
    
    int total_removed = 0;
    
    /* Process each drive in the database */
    for (int d = 0; d < db->drive_count; d++) {
        DriveData *drv = &db->drives[d];
        if (drv->dir_count == 0) continue;
        
        /* 
         * Build a mapping table: old_index -> new_index (or -1 if excluded).
         * Also mark which directories are excluded.
         */
        int *index_map = ncd_malloc_array((size_t)drv->dir_count, sizeof(int));
        bool *is_excluded = ncd_calloc((size_t)drv->dir_count, sizeof(bool));
        
        /* First pass: identify excluded directories */
        for (int i = 0; i < drv->dir_count; i++) {
            char full_path[MAX_PATH];
            db_full_path(drv, i, full_path, sizeof(full_path));
            
            /* 
             * db_exclusion_check expects path relative to drive root.
             * db_full_path returns "C:\Windows\System32" on Windows.
             * Skip past the "C:\" to get "Windows\System32".
             */
            const char *rel_path = full_path;
#if NCD_PLATFORM_WINDOWS
            if (rel_path[1] == ':') {
                rel_path += 2;  /* Skip "C:" */
                if (rel_path[0] == '\\' || rel_path[0] == '/') {
                    rel_path++;  /* Skip leading backslash */
                }
            }
#else
            /* Linux: full_path is the absolute path starting with / */
            if (rel_path[0] == '/') {
                rel_path++;  /* Skip leading slash */
            }
#endif
            
            /* Check if this directory matches any exclusion pattern */
            if (db_exclusion_check(meta, drv->letter, rel_path)) {
                is_excluded[i] = true;
                total_removed++;
            }
        }
        
        /* Second pass: mark children of excluded directories as excluded */
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < drv->dir_count; i++) {
                if (is_excluded[i]) continue;
                
                /* Check if parent is excluded */
                int parent = drv->dirs[i].parent;
                if (parent >= 0 && parent < drv->dir_count && is_excluded[parent]) {
                    is_excluded[i] = true;
                    total_removed++;
                    changed = true;
                }
            }
        }
        
        /* Build index mapping: calculate new index for each kept directory */
        int new_count = 0;
        for (int i = 0; i < drv->dir_count; i++) {
            if (is_excluded[i]) {
                index_map[i] = -1;  /* Mark as removed */
            } else {
                index_map[i] = new_count++;
            }
        }
        
        /* If nothing excluded, skip to next drive */
        if (new_count == drv->dir_count) {
            free(index_map);
            free(is_excluded);
            continue;
        }
        
        /* 
         * Second pass: rebuild the directory array.
         * We need to preserve the order and update parent indices.
         */
        DirEntry *new_dirs = ncd_malloc_array((size_t)new_count, sizeof(DirEntry));
        
        /* Build new name pool */
        size_t new_pool_size = 0;
        for (int i = 0; i < drv->dir_count; i++) {
            if (!is_excluded[i]) {
                const char *name = drv->name_pool + drv->dirs[i].name_off;
                new_pool_size += strlen(name) + 1;
            }
        }
        char *new_pool = ncd_malloc(new_pool_size);
        
        /* Copy directories to new arrays */
        int new_idx = 0;
        size_t pool_pos = 0;
        for (int i = 0; i < drv->dir_count; i++) {
            if (is_excluded[i]) continue;
            
            /* Copy directory entry */
            new_dirs[new_idx] = drv->dirs[i];
            
            /* Copy name to new pool */
            const char *name = drv->name_pool + drv->dirs[i].name_off;
            size_t name_len = strlen(name) + 1;
            memcpy(new_pool + pool_pos, name, name_len);
            new_dirs[new_idx].name_off = (uint32_t)pool_pos;
            pool_pos += name_len;
            
            /* Update parent index */
            if (drv->dirs[i].parent >= 0) {
                int old_parent = drv->dirs[i].parent;
                /* Parent might have been excluded - if so, mark as root (-1) */
                if (old_parent < drv->dir_count && is_excluded[old_parent]) {
                    new_dirs[new_idx].parent = -1;
                } else {
                    new_dirs[new_idx].parent = index_map[old_parent];
                }
            }
            
            new_idx++;
        }
        
        /* Replace old arrays with new ones */
        free(drv->dirs);
        free(drv->name_pool);
        
        drv->dirs = new_dirs;
        drv->dir_count = new_count;
        drv->dir_capacity = new_count;
        
        drv->name_pool = new_pool;
        drv->name_pool_len = pool_pos;
        drv->name_pool_cap = pool_pos;
        
        /* Invalidate cached name index since database structure changed */
        db->name_index_generation++;
        
        free(index_map);
        free(is_excluded);
    }
    
    return total_removed;
}

/* ============================================================ binary save */

/*
 * File layout written by db_save_binary():
 *
 *   [UTF16LE BOM : 2 bytes] (only for UTF16 mode)
 *   [BinFileHdr : 32 bytes]
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

/* Global text encoding mode for database I/O (NCD_TEXT_UTF8 or NCD_TEXT_UTF16LE) */
uint8_t g_db_text_encoding = NCD_TEXT_UTF8;

/* Set the text encoding mode for database I/O */
void db_set_text_encoding(uint8_t encoding)
{
    if (encoding == NCD_TEXT_UTF8 || encoding == NCD_TEXT_UTF16LE) {
        g_db_text_encoding = encoding;
    }
}

/* Get the current text encoding mode for database I/O */
uint8_t db_get_text_encoding(void)
{
    return g_db_text_encoding;
}

bool db_save_binary(const NcdDatabase *db, const char *path)
{
    /* ---- compute total output size (including optional BOM) ---- */
    size_t bom_size = (g_db_text_encoding == NCD_TEXT_UTF16LE) ? NCD_UTF16LE_BOM_LEN : 0;
    size_t total = bom_size + sizeof(BinFileHdr)
                 + (size_t)db->drive_count * sizeof(BinDriveHdr);

    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];
        size_t used = (size_t)drv->dir_count * sizeof(DirEntry)
                    + drv->name_pool_len;
        total += (used + 3) & ~(size_t)3;   /* pad to 4-byte boundary */
    }

    char *buf = ncd_malloc(total);
    size_t pos = 0;

    /* ---- UTF16LE BOM (if UTF16 mode) ---- */
    if (g_db_text_encoding == NCD_TEXT_UTF16LE) {
        memcpy(buf + pos, NCD_UTF16LE_BOM, NCD_UTF16LE_BOM_LEN);
        pos += NCD_UTF16LE_BOM_LEN;
    }

    /* ---- BinFileHdr ---- */
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = NCD_BIN_MAGIC;
    hdr.version     = NCD_BIN_VERSION;
    hdr.show_hidden = db->default_show_hidden ? 1 : 0;
    hdr.show_system = db->default_show_system ? 1 : 0;
    hdr.last_scan   = (int64_t)db->last_scan;
    hdr.drive_count = (uint32_t)db->drive_count;
    hdr.encoding    = g_db_text_encoding;
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
#if NCD_PLATFORM_LINUX
    /* Ensure data is physically on disk before renaming (POSIX durability) */
    if (fsync(fileno(f)) != 0) {
        db_set_error("Failed to fsync database to disk: %s", strerror(errno));
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
#endif
    fclose(f);
    free(buf);

    /* Atomic file replacement with backup:
     * 1. If final file exists, move it to .old (backup)
     * 2. Move .tmp to final name (atomic rename)
     * 3. On success, delete .old; on failure, restore from .old
     */
    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    
    /* Step 1: Backup existing file to .old if it exists */
    if (platform_file_exists(path)) {
        /* Remove any stale .old file first */
        platform_delete_file(old_path);
        if (!platform_move_file(path, old_path)) {
            db_set_error("Failed to backup existing database to .old: %s", strerror(errno));
            platform_delete_file(tmp_path);
            return false;
        }
    }
    
    /* Step 2: Atomic rename .tmp to final */
    if (!platform_move_file_replace(tmp_path, path)) {
        db_set_error("Failed to install database file: %s", strerror(errno));
        /* Try to restore from backup on failure */
        if (platform_file_exists(old_path)) {
            platform_move_file_replace(old_path, path);
        }
        platform_delete_file(tmp_path);
        return false;
    }
    
    /* Step 3: Success - remove backup */
    platform_delete_file(old_path);
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
    
    char *blob = ncd_malloc(sz);
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
    
    /* ---- check for UTF16LE BOM ---- */
    size_t bom_offset = 0;
    bool has_utf16_bom = false;
    if (sz >= NCD_UTF16LE_BOM_LEN) {
        const unsigned char *bom = (const unsigned char *)blob;
        if (bom[0] == 0xFF && bom[1] == 0xFE) {
            has_utf16_bom = true;
            bom_offset = NCD_UTF16LE_BOM_LEN;
        }
    }
    
    /* ---- validate header ---- */
    if (sz < bom_offset + sizeof(BinFileHdr)) {
        fprintf(stderr, "NCD: Database file too small (after BOM check)\n");
        free(blob);
        return NULL;
    }
    
    BinFileHdr hdr;
    memcpy(&hdr, blob + bom_offset, sizeof(hdr));
    
    if (!validate_file_header(&hdr, sz - bom_offset)) {
        free(blob);
        return NULL;
    }
    
    /* ---- validate encoding matches BOM ---- */
    if (has_utf16_bom && hdr.encoding != NCD_TEXT_UTF16LE) {
        fprintf(stderr, "NCD: Database has UTF16 BOM but header encoding is not UTF16\n");
        free(blob);
        return NULL;
    }
    if (!has_utf16_bom && hdr.encoding == NCD_TEXT_UTF16LE) {
        fprintf(stderr, "NCD: Database header claims UTF16 but no BOM found\n");
        free(blob);
        return NULL;
    }
    
    /* Set global encoding to match loaded file */
    g_db_text_encoding = hdr.encoding ? hdr.encoding : NCD_TEXT_UTF8;
    
    /* ---- verify CRC64 checksum over all data after the header ---- */
    /* Checksum covers data after header, starting from bom_offset + sizeof(hdr) */
#if DEBUG
    if (hdr.checksum != 0 && !g_test_no_checksum) {
#else
    if (hdr.checksum != 0) {
#endif
        uint64_t computed_checksum = platform_crc64(blob + bom_offset + sizeof(hdr), 
                                                      sz - bom_offset - sizeof(hdr));
        if (computed_checksum != hdr.checksum) {
            fprintf(stderr, "NCD: Database checksum mismatch (file may be corrupted)\n");
            free(blob);
            return NULL;
        }
    }
    /* If checksum is 0, the file was saved by an old version that didn't compute checksums.
     * We accept it for backward compatibility, but warn. */
    
    /* ---- pre-validate all drive headers before allocating ---- */
    const char *p = blob + bom_offset + sizeof(BinFileHdr);
    const char *dp = blob + bom_offset + sizeof(BinFileHdr)
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
    p  = blob + bom_offset + sizeof(BinFileHdr);
    dp = blob + bom_offset + sizeof(BinFileHdr)
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
 *   - Anything else                                       → unsupported
 *
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
    return NULL;
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
    
    /* Atomic file replacement */
    if (!platform_move_file_replace(tmp_path, path)) {
        platform_delete_file(tmp_path);
        return false;
    }
    return true;
}

/* ============================================================= metadata  */

/* Global override path for metadata file (set by -conf option) */
static char g_metadata_override[NCD_MAX_PATH] = {0};

void db_metadata_set_override(const char *path)
{
    if (path && path[0]) {
        platform_strncpy_s(g_metadata_override, sizeof(g_metadata_override), path);
    } else {
        g_metadata_override[0] = '\0';
    }
}

char *db_metadata_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NULL;
    buf[0] = '\0';
    
    /* Check for override first */
    if (g_metadata_override[0]) {
        platform_strncpy_s(buf, buf_size, g_metadata_override);
        return buf;
    }
    
#if NCD_PLATFORM_WINDOWS
    char local[MAX_PATH] = {0};
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return NULL;
    
    size_t len = strlen(local);
    bool has_sep = (len > 0 && (local[len - 1] == '\\' || local[len - 1] == '/'));
    snprintf(buf, buf_size, "%s%sNCD%s%s", local, has_sep ? "" : NCD_PATH_SEP, NCD_PATH_SEP, NCD_META_FILENAME);
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

char *db_logs_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NULL;
    buf[0] = '\0';
    
#if NCD_PLATFORM_WINDOWS
    /* Check for override first - use the directory containing the override file */
    if (g_metadata_override[0]) {
        char override_copy[MAX_PATH];
        platform_strncpy_s(override_copy, sizeof(override_copy), g_metadata_override);
        char *last_sep = strrchr(override_copy, '\\');
        if (!last_sep) last_sep = strrchr(override_copy, '/');
        if (last_sep) {
            *last_sep = '\0';
            platform_strncpy_s(buf, buf_size, override_copy);
            return buf;
        }
    }
    
    /* Default location: %LOCALAPPDATA%\NCD */
    char local[MAX_PATH];
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return NULL;
    
    int written = snprintf(buf, buf_size, "%s\\NCD", local);
    if (written <= 0 || (size_t)written >= buf_size) return NULL;
    
    /* Create the NCD directory if it doesn't exist */
    platform_create_dir(buf);
    
#else /* Linux */
    /* Check for override first - use the directory containing the override file */
    if (g_metadata_override[0]) {
        char override_copy[MAX_PATH];
        platform_strncpy_s(override_copy, sizeof(override_copy), g_metadata_override);
        char *last_sep = strrchr(override_copy, '/');
        if (last_sep) {
            *last_sep = '\0';
            platform_strncpy_s(buf, buf_size, override_copy);
            return buf;
        }
    }
    
    /* Default location: $XDG_DATA_HOME/ncd or ~/.local/share/ncd */
    char base[MAX_PATH];
    if (!platform_get_env("XDG_DATA_HOME", base, sizeof(base)) || !base[0]) {
        char home[MAX_PATH] = {0};
        if (!platform_get_env("HOME", home, sizeof(home)) || !home[0])
            return NULL;
        int written = snprintf(base, sizeof(base), "%s/.local/share", home);
        if (written <= 0 || (size_t)written >= sizeof(base)) return NULL;
    }
    
    /* Create the ncd directory if it doesn't exist */
    int written = snprintf(buf, buf_size, "%s/ncd", base);
    if (written <= 0 || (size_t)written >= buf_size) return NULL;
    platform_create_dir(buf);
#endif
    
    return buf;
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
        if (*p == '/') *p = NCD_PATH_SEP_CHAR;
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
        
        /* Get the pattern path (skip drive letter if present) and normalize */
        char norm_pattern[NCD_MAX_PATH];
        const char *pattern_start = entry->pattern;
#if NCD_PLATFORM_WINDOWS
        if (pattern_start[1] == ':') {
            pattern_start += 2;  /* Skip "C:" */
        }
#endif
        platform_strncpy_s(norm_pattern, sizeof(norm_pattern), pattern_start);
#if NCD_PLATFORM_WINDOWS
        /* Normalize forward slashes to backslashes on Windows */
        for (char *p = norm_pattern; *p; p++) {
            if (*p == '/') *p = NCD_PATH_SEP_CHAR;
        }
#endif
        const char *pattern = norm_pattern;
        
        /* Handle root-only matches */
        if (entry->match_from_root) {
            /* Skip leading separator in pattern for matching */
            if (pattern[0] == '\\' || pattern[0] == '/') {
                pattern++;
            }
            
            if (wildcard_match(pattern, path_to_match)) {
                return true;
            }
            
            /* Check if pattern matches as a directory prefix (e.g., "Windows" matches "Windows\System32") */
            size_t pattern_len = strlen(pattern);
#if NCD_PLATFORM_WINDOWS
            if (pattern_len > 0 && _strnicmp(path_to_match, pattern, pattern_len) == 0) {
#else
            if (pattern_len > 0 && strncasecmp(path_to_match, pattern, pattern_len) == 0) {
#endif
                /* Pattern matches the start of path - check if it's a directory boundary */
                char next_char = path_to_match[pattern_len];
                if (next_char == '\\' || next_char == '/' || next_char == '\0') {
                    return true;
                }
            }
        } else {
            /* Check if pattern matches this path component or any parent */
            /* First try matching the full remaining path */
            if (wildcard_match(pattern, path_to_match)) {
                return true;
            }
            
            /* For patterns like "*\dir\*.ext", also try matching without the leading "*\"
             * This handles root-level directories like "build\test.obj" matching "*\build\*.obj"
             */
            if (pattern[0] == '*' && (pattern[1] == '\\' || pattern[1] == '/')) {
                if (wildcard_match(pattern + 2, path_to_match)) {
                    return true;
                }
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
    db_exclusion_add(meta, "C:" NCD_PATH_SEP "Windows");
#else
    /* Linux default exclusions - pseudo-filesystems and system directories */
    db_exclusion_add(meta, "/proc");      /* Process information pseudo-filesystem */
    db_exclusion_add(meta, "/sys");       /* System information pseudo-filesystem */
    db_exclusion_add(meta, "/dev");       /* Device files */
    db_exclusion_add(meta, "/run");       /* Runtime variable data */
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
    /* Use file_path if set, otherwise use default metadata path */
    if (meta->file_path[0] != '\0') {
        platform_strncpy_s(path, sizeof(path), meta->file_path);
    } else {
        if (!db_metadata_path(path, sizeof(path))) return false;
    }
    
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
#if NCD_PLATFORM_LINUX
    /* Ensure data is physically on disk before renaming (POSIX durability) */
    if (fsync(fileno(f)) != 0) {
        db_set_error("Failed to fsync metadata to disk: %s", strerror(errno));
        fclose(f);
        free(buf);
        platform_delete_file(tmp_path);
        return false;
    }
#endif
    fclose(f);
    free(buf);
    
    if (wr != pos) {
        db_set_error("Failed to write metadata: incomplete write");
        platform_delete_file(tmp_path);
        return false;
    }
    
    /* Atomic file replacement with backup:
     * 1. If final file exists, move it to .old (backup)
     * 2. Move .tmp to final name (atomic rename)
     * 3. On success, delete .old; on failure, restore from .old
     */
    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    
    /* Step 1: Backup existing file to .old if it exists */
    if (platform_file_exists(path)) {
        /* Remove any stale .old file first */
        platform_delete_file(old_path);
        if (!platform_move_file(path, old_path)) {
            db_set_error("Failed to backup existing metadata to .old: %s", strerror(errno));
            platform_delete_file(tmp_path);
            return false;
        }
    }
    
    /* Step 2: Atomic rename .tmp to final */
    if (!platform_move_file_replace(tmp_path, path)) {
        db_set_error("Failed to install metadata file: %s", strerror(errno));
        /* Try to restore from backup on failure */
        if (platform_file_exists(old_path)) {
            platform_move_file_replace(old_path, path);
        }
        platform_delete_file(tmp_path);
        return false;
    }
    
    /* Step 3: Success - remove backup */
    platform_delete_file(old_path);
    
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
    if (!db_metadata_path(path, sizeof(path)) || !platform_file_exists(path)) {
        /* No metadata file and no legacy files - return empty metadata with defaults */
        db_exclusion_init_defaults(meta);
        return meta;
    }
    
    platform_strncpy_s(meta->file_path, sizeof(meta->file_path), path);
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        return meta;
    }
    
    /* Read header */
    MetaFileHdr hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return meta;
    }

    if (hdr.magic != NCD_META_MAGIC) {
        fclose(f);
        fprintf(stderr, "NCD: Error: Metadata file has invalid magic number (corrupted?).\n");
        db_metadata_free(meta);
        return NULL;
    }
    if (hdr.version != NCD_META_VERSION) {
        fclose(f);
        fprintf(stderr, "NCD: Error: Metadata file version mismatch (expected %d, got %d).\n",
                NCD_META_VERSION, hdr.version);
        db_metadata_free(meta);
        return NULL;
    }
    
    /* Get file size for CRC verification */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return meta;
    }
    long file_size = ftell(f);
    if (fseek(f, NCD_META_HEADER_SIZE, SEEK_SET) != 0) { /* Skip header */
        fclose(f);
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
        return meta;
    }
    fclose(f);
    
    /* Verify CRC64 over data after header */
    uint64_t computed_crc = platform_crc64(data, data_size);
    
    if (computed_crc != hdr.checksum) {
        /* CRC mismatch - fail hard like database files */
        free(data);
        fprintf(stderr, "NCD: Error: Metadata file CRC check failed (file corrupted?).\n");
        db_metadata_free(meta);
        return NULL;
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
                    /* Cap count to what can fit in the section_size */
                    uint32_t max_count = (uint32_t)((section_size - 4) / sizeof(NcdHeurEntryV2));
                    if (count > max_count) count = max_count;
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
