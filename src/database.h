/*
 * database.h  --  Load and save the NCD directory database
 */

#ifndef NCD_DATABASE_H
#define NCD_DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"

/* --------------------------------------------------------- lifecycle      */

/* Allocate an empty database. */
NcdDatabase *db_create(void);

/* Release all memory owned by db (including the db struct itself). */
void db_free(NcdDatabase *db);

/* --------------------------------------------------------- persistence    */

/*
 * Load database from path (JSON format, legacy).
 * Returns NULL on I/O or parse error.
 */
NcdDatabase *db_load(const char *path);

/*
 * Load database from path (binary format).
 * Returns NULL on I/O or format error.
 * On success, all directory data is mapped from a single heap buffer;
 * the caller frees it with the normal db_free().
 */
NcdDatabase *db_load_binary(const char *path);

/*
 * Auto-detecting load: inspects the first 4 bytes of path to decide
 * between binary and JSON format.  If the file is JSON, it is loaded
 * and then silently resaved in binary format for faster subsequent loads.
 * Returns NULL on I/O or parse error.
 */
NcdDatabase *db_load_auto(const char *path);

/*
 * Save database to path (JSON format, atomic rename).
 * Returns true on success.
 */
bool db_save(const NcdDatabase *db, const char *path);

/*
 * Save database to path (binary format, atomic rename).
 * Returns true on success.
 */
bool db_save_binary(const NcdDatabase *db, const char *path);

/* --------------------------------------------------------- path helpers   */

/*
 * Resolve the default database path into buf (size MAX_PATH).
 * Uses %LOCALAPPDATA%\ncd.database.
 * Returns buf on success, NULL on failure.
 */
char *db_default_path(char *buf, size_t buf_size);

/*
 * Resolve the per-drive database path into buf.
 * Returns e.g. %LOCALAPPDATA%\ncd_C.database for letter 'C'.
 * letter is upper-cased automatically.
 * Returns buf on success, NULL on failure.
 */
char *db_drive_path(char letter, char *buf, size_t buf_size);

/*
 * Save exactly one drive (drive_idx) to a binary file.
 * The output file has BinFileHdr.drive_count == 1.
 * Uses the same atomic .tmp -> rename pattern as db_save_binary().
 * Returns true on success.
 */
bool db_save_binary_single(const NcdDatabase *db, int drive_idx,
                            const char *path);

/* --------------------------------------------------------- version check  */

/* Return codes for db_check_file_version */
#define DB_VERSION_ERROR       0   /* File doesn't exist or error reading */
#define DB_VERSION_OK          1   /* Version matches current */
#define DB_VERSION_MISMATCH    2   /* Version mismatch, user NOT skipped */
#define DB_VERSION_SKIPPED     3   /* Version mismatch, user previously skipped */

/*
 * Check a single database file's version.
 * 
 * Returns one of the DB_VERSION_* codes above.
 * This is a fast check that only reads the header.
 */
int db_check_file_version(const char *path);

/*
 * Information about a database file's version status.
 */
typedef struct {
    char     letter;              /* Drive letter/mount index */
    char     path[MAX_PATH];      /* Full path to database file */
    uint16_t version;             /* Version found in file (0 if unreadable) */
    bool     user_skipped_update; /* Flag from header */
    bool     is_mounted;          /* Whether drive is currently mounted */
} DbVersionInfo;

/*
 * Check all database files to find those with version mismatches.
 * 
 * Pass an array of DbVersionInfo (size at least 26) in 'out'.
 * Returns the number of entries filled (0 if all up to date).
 * 
 * For each drive letter that has a database file, checks:
 *   - If file magic is valid
 *   - If version matches NCD_BIN_VERSION
 *   - If user_skipped_update flag is set
 *   - If drive is currently mounted
 * 
 * Only returns drives that need updating (wrong version AND not skipped).
 */
int db_check_all_versions(DbVersionInfo *out, int max_entries);

/*
 * Set the user_skipped_update flag on a database file without rescanning.
 * This marks the database as "user declined update" so they won't be
 * prompted again for this drive.
 * 
 * Returns true on success.
 */
bool db_set_skip_update_flag(const char *path);

/* --------------------------------------------------------- drive helpers  */

/* Add a blank DriveData for drive_letter; return pointer to it. */
DriveData *db_add_drive(NcdDatabase *db, char letter);

/* Find a DriveData by letter; return NULL if not present. */
DriveData *db_find_drive(NcdDatabase *db, char letter);

/* --------------------------------------------------------- dir helpers    */

/*
 * Add a directory entry to a drive.
 * Returns the assigned id (== current dir_count - 1 after insertion).
 */
int db_add_dir(DriveData *drv,
               const char *name,
               int32_t     parent,
               bool        is_hidden,
               bool        is_system);

/*
 * Reconstruct the full absolute path for a directory entry.
 * e.g. "C:\Users\Scott\Downloads"
 * Writes into buf (size NCD_MAX_PATH).  Returns buf.
 */
char *db_full_path(const DriveData *drv,
                   int              dir_index,
                   char            *buf,
                   size_t           buf_size);

/* --------------------------------------------------------- group database */

#define NCD_GROUP_MAGIC    0x47474442U   /* 'G' 'G' 'D' 'B' in little-endian */
#define NCD_GROUP_VERSION  1

#define NCD_MAX_GROUPS     256

/*
 * NcdGroupEntry - single group mapping
 */
typedef struct {
    char name[NCD_MAX_GROUP_NAME];  /* Group name (e.g., "@home") */
    char path[NCD_MAX_PATH];        /* Full directory path */
    time_t created;                 /* When group was created */
} NcdGroupEntry;

/*
 * NcdGroupDb - group database
 */
typedef struct {
    NcdGroupEntry *groups;
    int count;
    int capacity;
} NcdGroupDb;

/*
 * Get the path to the group database file.
 * Returns path in buf, or NULL on failure.
 */
char *db_group_path(char *buf, size_t buf_size);

/*
 * Create a new empty group database.
 */
NcdGroupDb *db_group_create(void);

/*
 * Free a group database and all its entries.
 */
void db_group_free(NcdGroupDb *gdb);

/*
 * Load group database from disk.
 * Returns NULL if file doesn't exist or is corrupt.
 */
NcdGroupDb *db_group_load(void);

/*
 * Save group database to disk.
 * Returns true on success.
 */
bool db_group_save(const NcdGroupDb *gdb);

/*
 * Add or update a group.
 * If group already exists, updates the path.
 * Returns true on success.
 */
bool db_group_set(NcdGroupDb *gdb, const char *name, const char *path);

/*
 * Remove a group by name.
 * Returns true if group was found and removed.
 */
bool db_group_remove(NcdGroupDb *gdb, const char *name);

/*
 * Look up a group by name.
 * Returns pointer to entry, or NULL if not found.
 */
const NcdGroupEntry *db_group_get(const NcdGroupDb *gdb, const char *name);

/*
 * List all groups to console.
 */
void db_group_list(const NcdGroupDb *gdb);

#ifdef __cplusplus
}
#endif

#endif /* NCD_DATABASE_H */
