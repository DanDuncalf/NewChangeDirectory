/*
 * scanner.c  --  Platform-agnostic directory scan and threading orchestration
 *
 * Platform-specific tasks (drive enumeration, file enumeration, etc.) are
 * delegated to platform.c via helper functions. This file contains only the
 * generic algorithm and per-mount worker thread logic.
 */

#include "scanner.h"
#include "database.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if NCD_PLATFORM_LINUX
#include <sys/stat.h>
#endif

/* ================================================================ directory entry attribute queries */

/* Platform-agnostic attribute bit constants for PlatformFindData */
#if NCD_PLATFORM_WINDOWS
#define DIR_ATTR_ISDIR    FILE_ATTRIBUTE_DIRECTORY
#define DIR_ATTR_HIDDEN   FILE_ATTRIBUTE_HIDDEN
#define DIR_ATTR_SYSTEM   FILE_ATTRIBUTE_SYSTEM
#define DIR_ATTR_LINK     FILE_ATTRIBUTE_REPARSE_POINT
#else
#define DIR_ATTR_ISDIR    (1UL << 0)
#define DIR_ATTR_HIDDEN   (1UL << 1)
#define DIR_ATTR_SYSTEM   (1UL << 2)
#define DIR_ATTR_LINK     (1UL << 3)
#endif

bool find_is_directory(const PlatformFindData *fd)
{
    return fd && (fd->attrs & DIR_ATTR_ISDIR);
}

bool find_is_hidden(const PlatformFindData *fd)
{
    return fd && (fd->attrs & DIR_ATTR_HIDDEN);
}

bool find_is_system(const PlatformFindData *fd)
{
    return fd && (fd->attrs & DIR_ATTR_SYSTEM);
}

bool find_is_reparse(const PlatformFindData *fd)
{
    return fd && (fd->attrs & DIR_ATTR_LINK);
}

/* ================================================================ tuning  */

/* Fire the progress callback after every Nth directory found per mount. */
#define PROGRESS_INTERVAL  100

/*
 * Activity-based timeout: milliseconds of inactivity before considering
 * a scan thread stuck. The scan only times out if no directories have
 * been found for this duration. This allows large/slow drives to complete
 * without being cut off prematurely.
 */
#define SCAN_INACTIVITY_TIMEOUT_MS  60000  /* 60 seconds */

/* ================================================================ context */

/*
 * Per-mount context passed down the recursive call stack.
 * Keeping it in a struct avoids a long argument list at every level.
 */
typedef struct {
    DriveData  *drv;
    bool        include_hidden;
    bool        include_system;
    DriveStatus *status;          /* shared with main-thread display loop    */
    int         dir_count;        /* local counter (mirrors status->dir_count)*/
    ScanProgressFn progress_fn;
    void       *progress_user_data;
} ScanCtx;

/* ============================================================= threading  */

/*
 * Per-mount thread data block.
 * drive_slot is pre-assigned before any thread starts, so threads never
 * modify db->drives[] -- only the DriveData at their assigned slot.
 */
typedef struct {
    NcdDatabase *db;
    int          drive_slot;     /* index into db->drives[]                 */
    bool         include_hidden;
    bool         include_system;
    DriveStatus *status;         /* shared with main-thread display loop    */
    int          dirs_found;     /* written on completion                   */
} ScanThreadData;

/* Platform-specific implementation of recursive directory scan.
 * This function is implemented separately for Windows and Linux
 * in the respective platform sections. */
static int platform_scan_directory(ScanCtx *ctx, const char *mount_path,
                                   int32_t parent_id);

/* Worker thread entry point (platform-agnostic). */
static unsigned long worker_thread(void *param)
{
    ScanThreadData *td  = (ScanThreadData *)param;
    DriveData      *drv = &td->db->drives[td->drive_slot];

    /* drv->label contains the mount path set by the caller */
    const char *mount = drv->label[0] ? drv->label : "/";

    /* Build scan context. */
    ScanCtx ctx = {
        .drv            = drv,
        .include_hidden = td->include_hidden,
        .include_system = td->include_system,
        .status         = td->status,
        .dir_count      = 0,
    };

    /* Perform platform-specific scan. */
    int count = platform_scan_directory(&ctx, mount, -1);

    /* Signal completion to the main-thread display loop. */
    td->status->final_count = count;
    platform_atomic_exchange(&td->status->is_done, 1);
    td->dirs_found = count;
    return 0;
}

/* ================================================================ public   */

int scan_mount(NcdDatabase   *db,
               const char    *mount,
               bool           include_hidden,
               bool           include_system,
               ScanProgressFn progress_fn,
               void          *user_data)
{
    (void)progress_fn;
    (void)user_data;

    if (!mount || !mount[0]) return -1;

    /* Validate mount string and derive drive letter for status display. */
    char drive_letter = platform_get_drive_letter(mount);

    /* Remove stale data for this mount if re-scanning. */
    for (int i = 0; i < db->drive_count; i++) {
        if (db->drives[i].letter == drive_letter) {
            free(db->drives[i].dirs);
            free(db->drives[i].name_pool);
            memmove(&db->drives[i], &db->drives[i + 1],
                    (size_t)(db->drive_count - i - 1) * sizeof(DriveData));
            db->drive_count--;
            break;
        }
    }

    DriveData *drv = db_add_drive(db, drive_letter);
    drv->type = platform_get_mount_type(mount);
    platform_strncpy_s(drv->label, sizeof(drv->label), mount);

    /* Always run the scan in a worker thread. Build a ScanThreadData
     * and spawn the worker, then wait for completion. */
    DriveStatus local_status;
    memset(&local_status, 0, sizeof(local_status));
    local_status.drive_letter = drive_letter;

#if NCD_PLATFORM_LINUX
    /*
     * On Linux/WSL, if scanning root (/), only scan specific system directories
     * to avoid duplicates with /mnt/X drives and WSL internal mounts.
     * We scan: /etc, /home, /usr, /var, /root, /opt
     */
    if (strcmp(mount, "/") == 0) {
        static const char *root_dirs[] = {"/etc", "/home", "/usr", "/var", "/root", "/opt", NULL};
        int total_count = 0;
        
        /* Add root entry first */
        int root_id = db_add_dir(drv, "/", -1, false, false);
        total_count++;
        
        for (int i = 0; root_dirs[i]; i++) {
            struct stat st;
            if (stat(root_dirs[i], &st) != 0 || !S_ISDIR(st.st_mode))
                continue;
            
            /* Extract directory name */
            const char *dirname = root_dirs[i] + 1; /* skip leading / */
            
            /* Add this system dir under root */
            int dir_id = db_add_dir(drv, dirname, root_id, false, false);
            total_count++;
            
            /* Scan its contents */
            ScanCtx ctx = {
                .drv            = drv,
                .include_hidden = include_hidden,
                .include_system = include_system,
                .status         = &local_status,
                .dir_count      = 0,
            };
            total_count += platform_scan_directory(&ctx, root_dirs[i], (int32_t)dir_id);
        }
        
        local_status.final_count = total_count;
        local_status.is_done = 1;
        return total_count;
    }
#endif

    ScanThreadData td;
    td.db = db;
    td.drive_slot = db->drive_count - 1; /* db_add_drive appended */
    td.include_hidden = include_hidden;
    td.include_system = include_system;
    td.status = &local_status;
    td.dirs_found = 0;

    PlatformHandle h = platform_thread_create(worker_thread, &td);
    if (h) {
        platform_thread_wait(h, 0xFFFFFFFFUL);
        platform_handle_close(h);
    } else {
        /* If thread creation fails, run inline in this thread. */
        worker_thread(&td);
    }

    return td.dirs_found;
}

/* ============================================================== API       */

int scan_mounts(NcdDatabase *db,
                const char  **mounts,
                int           count,
                bool          include_hidden,
                bool          include_system,
                int           timeout_seconds)
{
    /* Convert timeout to milliseconds, ensure reasonable minimum */
    unsigned long timeout_ms = timeout_seconds > 0 ? (unsigned long)timeout_seconds * 1000UL : 60000UL;

    /* Buffers for auto-enumerated mounts - must persist for the whole function */
    static char mount_bufs[26][MAX_PATH];
    static const char *mount_ptrs[26];

    /* If no mounts specified, enumerate them via platform code. */
    if (count == 0 || !mounts) {
        int mount_count = platform_enumerate_mounts(mount_bufs, mount_ptrs,
                                                     sizeof(mount_bufs[0]),
                                                     sizeof(mount_ptrs) / sizeof(mount_ptrs[0]));
        if (mount_count <= 0) return 0;

        mounts = mount_ptrs;
        count = mount_count;
    }

    if (count == 0) return 0;

    /* Pre-allocate DriveData slots so worker threads can safely reference
     * db->drives[] without races. */
    int slots[26];
    for (int i = 0; i < count && i < 26; i++) {
        char drive_letter = platform_get_drive_letter(mounts[i]);
        DriveData *drv = db_add_drive(db, drive_letter);
        drv->type = 0;
        platform_strncpy_s(drv->label, sizeof(drv->label), mounts[i]);
        slots[i] = db->drive_count - 1;
    }

    DriveStatus statuses[26];
    memset(statuses, 0, sizeof(statuses));
    unsigned long now = platform_tick_ms();
    for (int i = 0; i < count && i < 26; i++) {
        statuses[i].drive_letter = platform_get_drive_letter(mounts[i]);
        statuses[i].last_active_ms = now;  /* initialize activity timestamp */
    }

    PlatformHandle con = platform_console_open_out(false);
    bool has_con = (con != NULL);
    int con_width = 79;
    int status_start_row = 0;
    if (has_con) {
        PlatformConsoleInfo cinfo;
        if (platform_console_get_info(con, &cinfo)) {
            con_width = cinfo.buffer_w > 2 ? cinfo.buffer_w - 2 : 79;
            if (con_width > 200) con_width = 200;
        }
        char header[256] = "  Scanning mounts:";
        for (int i = 0; i < count && i < 26; i++) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), " %s", mounts[i]);
            platform_strncat_s(header, sizeof(header), tmp);
        }
        platform_strncat_s(header, sizeof(header), "\n");
        platform_console_write(con, header);

        PlatformConsoleInfo cinfo2;
        if (platform_console_get_info(con, &cinfo2))
            status_start_row = cinfo2.cursor_y;

        for (int i = 0; i < count && i < 26; i++) {
            char content[256];
            snprintf(content, sizeof(content), "  [%-30.30s] (starting...)", mounts[i]);
            char line[512];
            snprintf(line, sizeof(line), "%*.*s\n", con_width, con_width, content);
            platform_console_write(con, line);
        }
    }

    ScanThreadData td[26];
    PlatformHandle handles[26];
    int nhandles = 0;
    for (int i = 0; i < count && i < 26; i++) {
        td[i].db = db;
        td[i].drive_slot = slots[i];
        td[i].include_hidden = include_hidden;
        td[i].include_system = include_system;
        td[i].status = &statuses[i];
        td[i].dirs_found = 0;
        handles[nhandles] = platform_thread_create(worker_thread, &td[i]);
        if (handles[nhandles])
            nhandles++;
        else
            worker_thread(&td[i]);
    }

    /* Track previous dir_count to detect activity */
    LONG prev_dir_count[26];
    memset(prev_dir_count, 0, sizeof(prev_dir_count));

    for (;;) {
        bool all_done = true;
        unsigned long current_time = platform_tick_ms();

        for (int i = 0; i < count && i < 26; i++) {
            if (statuses[i].is_done) continue;
            all_done = false;

            /* Check if this mount made progress since last iteration */
            LONG current_count = statuses[i].dir_count;
            if (current_count != prev_dir_count[i]) {
                /* Activity detected - update last_active_ms */
                prev_dir_count[i] = current_count;
                statuses[i].last_active_ms = current_time;
            }
        }

        /* Check for inactivity timeout per mount */
        bool any_timed_out = false;
        for (int i = 0; i < count && i < 26; i++) {
            if (statuses[i].is_done) continue;
            unsigned long inactive_ms = current_time - statuses[i].last_active_ms;
            if (inactive_ms >= timeout_ms) {
                any_timed_out = true;
                break;
            }
        }

        if (has_con) {
            for (int i = 0; i < count && i < 26; i++) {
                DriveStatus *s = &statuses[i];
                char content[512];
                if (s->is_done) {
                    snprintf(content, sizeof(content),
                             "  [%-30s] COMPLETE (%d directories)",
                             mounts[i], s->final_count);
                } else if (!s->is_done &&
                           (current_time - s->last_active_ms) >= timeout_ms) {
                    snprintf(content, sizeof(content),
                             "  [%-30s] WARNING: scan timed out (inactive)",
                             mounts[i]);
                } else {
                    snprintf(content, sizeof(content),
                             "  [%-30s] %5ld scanned  %s",
                             mounts[i],
                             (long)s->dir_count,
                             s->current_path[0] ? s->current_path : "(starting...)");
                }
                platform_console_fill_char(con, status_start_row + i, 0, con_width, ' ');
                platform_console_fill_attr_default(con, status_start_row + i, 0, con_width);
                platform_console_write_at(con, status_start_row + i, 0, content);
            }
        }

        if (all_done || any_timed_out)
            break;
        platform_sleep_ms(100);
    }

    if (has_con) {
        platform_console_set_cursor(con, status_start_row + count, 0);
        platform_handle_close(con);
    }

    for (int j = 0; j < nhandles; j++)
        if (handles[j]) {
            platform_thread_wait(handles[j], 0xFFFFFFFFUL);
            platform_handle_close(handles[j]);
        }

    int total = 0;
    for (int i = 0; i < count && i < 26; i++)
        total += td[i].dirs_found;

    return total;
}

/* ============================================================ platform-specific scan implementations */

#if NCD_PLATFORM_WINDOWS

#include <windows.h>

static int platform_scan_directory(ScanCtx *ctx, const char *mount_path, int32_t parent_id)
{
    /* Build search pattern (e.g. "C:\*") */
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s*", mount_path) >= (int)sizeof(pattern))
        return 0;

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileExA(pattern, FindExInfoBasic, &fd,
                                FindExSearchLimitToDirectories, NULL,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    int dir_count = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (fd.cFileName[0] == '.' && (fd.cFileName[1] == '\0' ||
            (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;

        bool is_hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        bool is_system = (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        if (is_hidden && !ctx->include_hidden)
            continue;
        if (is_system && !ctx->include_system)
            continue;

        int my_id = db_add_dir(ctx->drv, fd.cFileName, parent_id, is_hidden, is_system);
        dir_count++;
        ctx->dir_count++;

        /* Build full child path */
        char child[MAX_PATH];
        size_t mlen = strlen(mount_path);
        bool needs_sep = (mlen > 0 && mount_path[mlen - 1] != '\\');
        if (snprintf(child, sizeof(child), "%s%s%s", mount_path,
                     needs_sep ? "\\" : "", fd.cFileName) >= (int)sizeof(child))
            continue;

        platform_atomic_inc(&ctx->status->dir_count);
        snprintf(ctx->status->current_path, sizeof(ctx->status->current_path), "%s", child);
        if (ctx->progress_fn && (ctx->dir_count % PROGRESS_INTERVAL) == 1)
            ctx->progress_fn(ctx->drv->letter, child, ctx->progress_user_data);

        dir_count += platform_scan_directory(ctx, child, (int32_t)my_id);

    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return dir_count;
}

#elif NCD_PLATFORM_LINUX

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int platform_scan_directory(ScanCtx *ctx, const char *mount_path, int32_t parent_id)
{
    DIR *d = opendir(mount_path);
    if (!d)
        return 0;

    int dir_count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
            (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        char child[MAX_PATH];
        if (snprintf(child, sizeof(child), "%s%s%s", mount_path,
                     mount_path[strlen(mount_path) - 1] == '/' ? "" : "/",
                     ent->d_name) >= (int)sizeof(child))
            continue;

        struct stat st;
        if (lstat(child, &st) != 0)
            continue;
        if (!S_ISDIR(st.st_mode))
            continue;

        bool is_hidden = (ent->d_name[0] == '.');
        bool is_system = false;

        if (is_hidden && !ctx->include_hidden)
            continue;

        int my_id = db_add_dir(ctx->drv, ent->d_name, parent_id, is_hidden, is_system);
        dir_count++;
        ctx->dir_count++;

        platform_atomic_inc(&ctx->status->dir_count);
        snprintf(ctx->status->current_path, sizeof(ctx->status->current_path), "%s", child);
        if (ctx->progress_fn && (ctx->dir_count % PROGRESS_INTERVAL) == 1)
            ctx->progress_fn(ctx->drv->letter, child, ctx->progress_user_data);

        dir_count += platform_scan_directory(ctx, child, (int32_t)my_id);
    }

    closedir(d);
    return dir_count;
}

#else
#error Unsupported platform in scanner.c
#endif
