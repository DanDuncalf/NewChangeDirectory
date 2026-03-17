/*
 * scanner.h  --  Enumerate directories into an NcdDatabase
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
 * Progress callback type for scan_mount.
 */
typedef void (*ScanProgressFn)(char drive_letter,
                               const char *current_path,
                               void       *user_data);

/*
 * Scan a list of platform-agnostic mount strings and populate db.
 *
 * `mounts` is an array of `count` NUL-terminated mount identifiers:
 *   Windows: "C:\\", "D:\\", etc.
 *   Linux:   "/", "/home", "/mnt/data", etc.
 *
 * If `count` is zero, the implementation should enumerate all mounts
 * (platform-specific enumeration delegated to platform_*.c).
 *
 * Options:
 *   include_hidden  -- include hidden/dot directories
 *   include_system  -- include system directories (Windows-specific)
 *
 * Progress is displayed internally by the main thread: one status line per
 * mount is rewritten every 100 ms until all worker threads complete.
 *
 * Returns number of directories added across all scanned mounts.
 */
int scan_mounts(NcdDatabase *db,
                const char  **mounts,
                int           count,
                bool          include_hidden, 
                bool          include_system); 

/*
 * Re-scan a single mount (platform-specific full path). This is the worker
 * function invoked by scan_mounts for each mount. Returns number of
 * directories found, or -1 on error. Progress callback is optional.
 */
int scan_mount(NcdDatabase   *db,
               const char    *mount,
               bool           include_hidden,
               bool           include_system,
               ScanProgressFn progress_fn,
               void          *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NCD_SCANNER_H */
