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

#ifdef __cplusplus
}
#endif

#endif /* NCD_DATABASE_H */
