/*
 * scanner.h  --  Enumerate drives and directories into an NcdDatabase
 */

#ifndef NCD_SCANNER_H
#define NCD_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"
#include "database.h"

/*
 * DriveStatus  --  live per-drive scan state shared between a worker thread
 *                  and the main-thread display loop.
 *
 * Fields written by the worker are declared volatile.  current_path may
 * tear on a read -- that is acceptable for a progress display.
 */
typedef struct {
    char          drive_letter;
    volatile LONG dir_count;           /* incremented for every directory    */
    char          current_path[MAX_PATH]; /* last path seen (approx, racy)   */
    volatile LONG is_done;             /* set to 1 when thread completes     */
    int           final_count;         /* total dirs found, written at end   */
} DriveStatus;

/*
 * Scan all accessible drives (fixed, removable, network) and populate db.
 *
 * Options:
 *   include_hidden  -- include directories with FILE_ATTRIBUTE_HIDDEN
 *   include_system  -- include directories with FILE_ATTRIBUTE_SYSTEM
 *
 * Progress is displayed internally by the main thread: one status line per
 * drive is rewritten every 100 ms until all worker threads complete.
 *
 * Returns number of directories added across all drives.
 */
int scan_all_drives(NcdDatabase *db,
                    bool         include_hidden,
                    bool         include_system);

/*
 * Progress callback type kept for scan_drive's public API.
 */
typedef void (*ScanProgressFn)(char drive_letter,
                               const char *current_path,
                               void       *user_data);

/*
 * Re-scan a single drive; replaces the existing DriveData for that drive
 * (or adds a new one if it wasn't present).
 *
 * Returns number of directories found, or -1 on error.
 */
int scan_drive(NcdDatabase   *db,
               char           drive_letter,
               bool           include_hidden,
               bool           include_system,
               ScanProgressFn progress_fn,
               void          *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NCD_SCANNER_H */
