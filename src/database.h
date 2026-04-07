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

/* Debug test flags (debug builds only) */
#if DEBUG
extern bool g_test_no_checksum;
extern bool g_test_slow_mode;
#endif

/* Get the last error message from a failed database operation. */
const char *db_get_last_error(void);

/* Allocate an empty database. */
NcdDatabase *db_create(void);

/* Release all memory owned by db (including the db struct itself). */
void db_free(NcdDatabase *db);

/*
 * Convert a blob-loaded database into a fully mutable one.
 * After this call, db->is_blob is false and all per-drive arrays
 * (dirs, name_pool) are independent heap allocations.
 */
void db_make_mutable(NcdDatabase *db);

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
#define DB_VERSION_MISMATCH    2   /* Version mismatch, rescan not yet skipped */
#define DB_VERSION_SKIPPED     3   /* Version mismatch, user skipped rescan    */

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
    bool     skipped_rescan; /* Flag from header */
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
 *   - If skipped_rescan flag is set
 *   - If drive is currently mounted
 * 
 * Only returns drives that need updating (wrong version AND not skipped).
 */
int db_check_all_versions(DbVersionInfo *out, int max_entries);

/*
 * Set the skipped_rescan flag on a database file.
 * This marks the database as "user declined rescan after format change"
 * so they won't be prompted again for this drive.
 * 
 * Returns true on success.
 */
bool db_set_skipped_rescan_flag(const char *path);

/* --------------------------------------------------------- text encoding    */

/*
 * Set the text encoding mode for database I/O.
 * Valid values: NCD_TEXT_UTF8 (1) or NCD_TEXT_UTF16LE (2)
 * Default is UTF8.
 */
void db_set_text_encoding(uint8_t encoding);

/*
 * Get the current text encoding mode for database I/O.
 * Returns NCD_TEXT_UTF8 or NCD_TEXT_UTF16LE.
 */
uint8_t db_get_text_encoding(void);

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

/*
 * Filter a loaded database to remove directories that match exclusion patterns.
 * This should be called after loading a database to ensure excluded directories
 * from previous scans are not shown in search results.
 * 
 * Returns the number of directories removed.
 */
int db_filter_excluded(NcdDatabase *db, NcdMetadata *meta);

/* --------------------------------------------------------- group database */

#define NCD_GROUP_MAGIC    0x47474442U   /* 'G' 'G' 'D' 'B' in little-endian */
#define NCD_GROUP_VERSION  1

#define NCD_MAX_GROUPS     256

/*
 * Get the path to the group database file.
 * DEPRECATED: Groups are now stored in metadata. Kept for migration.
 */
char *db_group_path(char *buf, size_t buf_size);

/*
 * Legacy: Create a new empty group database.
 * DEPRECATED: Groups are now stored in metadata.
 */
NcdGroupDb *db_group_create(void);

/*
 * Legacy: Free a group database and all its entries.
 * DEPRECATED: Groups are now stored in metadata.
 */
void db_group_free(NcdGroupDb *gdb);

/*
 * Legacy: Load group database from disk (for migration).
 * Returns NULL if file doesn't exist or is corrupt.
 */
NcdGroupDb *db_group_load(void);

/*
 * Legacy: Save group database to disk.
 * DEPRECATED: Groups are now stored in metadata.
 */
bool db_group_save(const NcdGroupDb *gdb);

/*
 * Add or update a group in metadata.
 * If group already exists, updates the path.
 * Returns true on success.
 */
bool db_group_set(NcdMetadata *meta, const char *name, const char *path);

/*
 * Remove a group by name from metadata.
 * Returns true if group was found and removed.
 */
bool db_group_remove(NcdMetadata *meta, const char *name);

/*
 * Look up a group by name in metadata.
 * Returns pointer to entry, or NULL if not found.
 * If multiple entries exist with the same name, returns the first one.
 */
const NcdGroupEntry *db_group_get(NcdMetadata *meta, const char *name);

/*
 * Look up all entries for a group name in metadata.
 * Fills out array with pointers to matching entries (up to max_out).
 * Returns the number of entries found (may be more than max_out).
 */
int db_group_get_all(NcdMetadata *meta, const char *name,
                     const NcdGroupEntry **out, int max_out);

/*
 * Remove a specific path from a group.
 * Returns true if entry was found and removed.
 */
bool db_group_remove_path(NcdMetadata *meta, const char *name, const char *path);

/*
 * List all groups to console from metadata.
 * Groups with multiple entries are shown with entry counts.
 */
void db_group_list(NcdMetadata *meta);

/* --------------------------------------------------------- configuration  */

/*
 * Get the path to the configuration file.
 * Returns path in buf, or NULL on failure.
 * 
 * DEPRECATED: Use db_metadata_path() instead for new code.
 * Kept for backward compatibility and migration.
 */
char *db_config_path(char *buf, size_t buf_size);

/*
 * Load configuration from disk.
 * Returns true on success, false if file doesn't exist or is corrupt.
 * On failure, fills cfg with default values.
 * 
 * DEPRECATED: Use db_metadata_load() instead for new code.
 * This function will try ncd.metadata first, then fall back to ncd.config.
 */
bool db_config_load(NcdConfig *cfg);

/*
 * Save configuration to disk.
 * Returns true on success.
 * 
 * DEPRECATED: Use db_metadata_save() instead for new code.
 * This function now saves to ncd.metadata, not ncd.config.
 */
bool db_config_save(const NcdConfig *cfg);

/*
 * Initialize config with default values.
 */
void db_config_init_defaults(NcdConfig *cfg);

/*
 * Check if configuration file exists.
 * 
 * DEPRECATED: Use db_metadata_exists() instead.
 * Returns true if either ncd.metadata or ncd.config exists.
 */
bool db_config_exists(void);

/* --------------------------------------------------------- consolidated metadata  */

/*
 * Get the path to the consolidated metadata file.
 * Returns path in buf, or NULL on failure.
 * If db_metadata_set_override() was called, returns that path instead.
 */
char *db_metadata_path(char *buf, size_t buf_size);

/*
 * Set a global override path for the metadata file.
 * This affects all subsequent db_metadata_path() calls.
 * Pass NULL to clear the override.
 */
void db_metadata_set_override(const char *path);

/*
 * Check if consolidated metadata file exists.
 */
bool db_metadata_exists(void);

/*
 * Create a new empty metadata container.
 * Call db_metadata_free() when done.
 */
NcdMetadata *db_metadata_create(void);

/*
 * Free a metadata container and all its contents.
 */
void db_metadata_free(NcdMetadata *meta);

/*
 * Load metadata from disk.
 * Tries ncd.metadata first. If not found, migrates from legacy files
 * (ncd.config, ncd.groups, ncd.history) and creates ncd.metadata.
 * 
 * Returns metadata container on success, or NULL on failure.
 * On failure, you can still use the returned container with default values.
 */
NcdMetadata *db_metadata_load(void);

/*
 * Save metadata to disk (atomic rename).
 * Returns true on success.
 */
bool db_metadata_save(NcdMetadata *meta);

/*
 * Migrate from legacy files to consolidated metadata.
 * Called automatically by db_metadata_load() if ncd.metadata doesn't exist.
 * Returns true on success, false if no legacy files found or migration failed.
 */


/*
 * Delete legacy files after successful migration.
 * Returns number of files deleted.
 */
int db_metadata_cleanup_legacy(void);

/* --------------------------------------------------------- enhanced heuristics  */

/*
 * Calculate heuristic score for an entry.
 * Formula: score = (frequency * 10) / (1 + (days_since_last_use / 7))
 * Higher score = more relevant result.
 */
int db_heur_calculate_score(const NcdHeurEntryV2 *entry, time_t now);

/*
 * Find a heuristics entry by search term (case-insensitive).
 * Returns index in meta->heuristics.entries, or -1 if not found.
 */
int db_heur_find(NcdMetadata *meta, const char *search);

/*
 * Find best matching heuristic entry using partial matching.
 * Returns index of best match, or -1 if no suitable match found.
 * 
 * If exact_match is not NULL, sets it to true if the search term
 * matches exactly (for priority boosting).
 */
int db_heur_find_best(NcdMetadata *meta, const char *search, bool *exact_match);

/*
 * Record a user choice in heuristics.
 * Increments frequency, updates last_used, and reorders by score.
 */
void db_heur_note_choice(NcdMetadata *meta, const char *search, const char *target);

/*
 * Get the preferred target path for a search term.
 * Returns true and fills out_path if a match is found.
 */
bool db_heur_get_preferred(NcdMetadata *meta, const char *search, 
                           char *out_path, size_t out_size);

/*
 * Clear all heuristics entries.
 */
void db_heur_clear(NcdMetadata *meta);

/*
 * Print heuristics entries sorted by score.
 */
void db_heur_print(NcdMetadata *meta);

/* --------------------------------------------------------- exclusion list  */

/*
 * Add an exclusion pattern to the list.
 * Pattern format: [drive:][\]path with * wildcards (Windows) or [/]path with * wildcards (Linux)
 * Returns true on success, false if pattern is invalid or list is full.
 */
bool db_exclusion_add(NcdMetadata *meta, const char *pattern);

/*
 * Remove an exclusion pattern from the list.
 * Returns true if pattern was found and removed, false if not found.
 */
bool db_exclusion_remove(NcdMetadata *meta, const char *pattern);

/*
 * Check if a directory matches any exclusion pattern.
 * drive_letter: the drive containing the directory
 * dir_path: full directory path (relative to drive root)
 * Returns true if the directory should be excluded.
 */
bool db_exclusion_check(NcdMetadata *meta, char drive_letter, const char *dir_path);

/*
 * Print all exclusion patterns.
 */
void db_exclusion_print(NcdMetadata *meta);

/*
 * Initialize default exclusion patterns (called on first run).
 */
void db_exclusion_init_defaults(NcdMetadata *meta);

/* --------------------------------------------------------- atomic rescan helpers  */

/*
 * Backup a drive's data before rescan.
 * Returns a dynamically allocated DriveData that must be freed with
 * db_drive_backup_free() when no longer needed.
 */
DriveData *db_drive_backup_create(const DriveData *src);

/*
 * Free a drive backup created by db_drive_backup_create().
 */
void db_drive_backup_free(DriveData *backup);

/*
 * Restore drive data from backup on failed rescan.
 */
void db_drive_restore_from_backup(DriveData *dst, const DriveData *backup);

/* --------------------------------------------------------- directory history  */

/*
 * Add a directory to the history list.
 * If the path already exists in the list, it is moved to the top (position 0).
 * If the list is full, the oldest entry is discarded.
 * Returns true on success.
 */
bool db_dir_history_add(NcdMetadata *meta, const char *path, char drive);

/*
 * Get a directory from the history list by index.
 * Index 0 is the most recent, index count-1 is the oldest.
 * Returns pointer to entry, or NULL if index is out of range.
 */
const NcdDirHistoryEntry *db_dir_history_get(NcdMetadata *meta, int index);

/*
 * Get a directory from history by display index (1-based, FIFO order).
 * Display index 1 is the oldest, display index count is the most recent.
 * Returns pointer to entry, or NULL if index is out of range (1 to count).
 */
const NcdDirHistoryEntry *db_dir_history_get_by_display_index(NcdMetadata *meta, int display_index);

/*
 * Swap the first two entries in the history list (ping-pong).
 * Only swaps if there are at least 2 entries.
 */
void db_dir_history_swap_first_two(NcdMetadata *meta);

/*
 * Get the number of entries in the history list.
 */
int db_dir_history_count(NcdMetadata *meta);

/*
 * Clear all history entries.
 */
void db_dir_history_clear(NcdMetadata *meta);

/*
 * Remove a single history entry by index.
 * Index 0 is the most recent entry.
 * Returns true on success, false if index is out of range.
 */
bool db_dir_history_remove(NcdMetadata *meta, int index);

/*
 * Print the directory history list (for debugging/display).
 */
void db_dir_history_print(NcdMetadata *meta);

#ifdef __cplusplus
}
#endif

#endif /* NCD_DATABASE_H */
