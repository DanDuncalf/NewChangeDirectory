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
#include "ncd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if NCD_PLATFORM_LINUX
#include <sys/stat.h>
#include <stdint.h>
#include <dirent.h>

/* ================================================================ bind mount cycle detection (Linux) */

/* Device+Inode pair for cycle detection */
typedef struct {
    dev_t dev;
    ino_t ino;
} DirId;

/* Simple hash set for DirId using open addressing */
typedef struct {
    DirId *entries;
    size_t count;
    size_t capacity;
} DirIdSet;

static uint64_t dirid_hash(DirId id) {
    return ((uint64_t)(id.dev) << 32) ^ (uint64_t)(id.ino);
}

static bool dirid_eq(DirId a, DirId b) {
    return a.dev == b.dev && a.ino == b.ino;
}

static DirIdSet *diridset_create(void) {
    DirIdSet *set = (DirIdSet *)malloc(sizeof(DirIdSet));
    if (!set) return NULL;
    set->capacity = 256;
    set->entries = (DirId *)calloc(set->capacity, sizeof(DirId));
    if (!set->entries) { free(set); return NULL; }
    set->count = 0;
    return set;
}

static void diridset_free(DirIdSet *set) {
    if (!set) return;
    free(set->entries);
    free(set);
}

static bool diridset_contains(DirIdSet *set, DirId id) {
    if (!set || set->count == 0) return false;
    size_t idx = (size_t)(dirid_hash(id) % set->capacity);
    size_t start = idx;
    while (set->entries[idx].dev != 0 || set->entries[idx].ino != 0) {
        if (dirid_eq(set->entries[idx], id)) return true;
        idx = (idx + 1) % set->capacity;
        if (idx == start) break;
    }
    return false;
}

static bool diridset_add(DirIdSet *set, DirId id) {
    if (!set) return false;
    /* Check if needs resize (70% load factor) */
    if (set->count >= set->capacity * 7 / 10) {
        size_t newcap = set->capacity * 2;
        DirId *newentries = (DirId *)calloc(newcap, sizeof(DirId));
        if (!newentries) return false;
        /* Rehash all existing entries */
        for (size_t i = 0; i < set->capacity; i++) {
            if (set->entries[i].dev != 0 || set->entries[i].ino != 0) {
                size_t idx = (size_t)(dirid_hash(set->entries[i]) % newcap);
                while (newentries[idx].dev != 0 || newentries[idx].ino != 0) {
                    idx = (idx + 1) % newcap;
                }
                newentries[idx] = set->entries[i];
            }
        }
        free(set->entries);
        set->entries = newentries;
        set->capacity = newcap;
    }
    size_t idx = (size_t)(dirid_hash(id) % set->capacity);
    while (set->entries[idx].dev != 0 || set->entries[idx].ino != 0) {
        if (dirid_eq(set->entries[idx], id)) return true; /* Already exists */
        idx = (idx + 1) % set->capacity;
    }
    set->entries[idx] = id;
    set->count++;
    return true;
}

#endif /* NCD_PLATFORM_LINUX */

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
#if NCD_PLATFORM_LINUX
    DirIdSet   *visited;          /* Track visited (dev,ino) for cycle detection */
#endif
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
#if NCD_PLATFORM_LINUX
        .visited        = diridset_create(),
#endif
    };

    /* Perform platform-specific scan. */
    int count = platform_scan_directory(&ctx, mount, -1);

#if NCD_PLATFORM_LINUX
    diridset_free(ctx.visited);
#endif

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
#if NCD_PLATFORM_LINUX
                .visited        = diridset_create(),
#endif
            };
            total_count += platform_scan_directory(&ctx, root_dirs[i], (int32_t)dir_id);
#if NCD_PLATFORM_LINUX
            diridset_free(ctx.visited);
#endif
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
        char header[512];
        snprintf(header, sizeof(header), "  Scanning %d mount%s:", count, count == 1 ? "" : "s");
        int header_len = (int)strlen(header);
        for (int i = 0; i < count && i < 26; i++) {
            /* Truncate header to fit console width */
            if (header_len + strlen(mounts[i]) + 1 >= (size_t)con_width - 3) {
                platform_strncat_s(header, sizeof(header), " ...");
                break;
            }
            char tmp[64];
            snprintf(tmp, sizeof(tmp), " %s", mounts[i]);
            platform_strncat_s(header, sizeof(header), tmp);
            header_len = (int)strlen(header);
        }
        platform_strncat_s(header, sizeof(header), "\n");
        platform_console_write(con, header);

        PlatformConsoleInfo cinfo2;
        if (platform_console_get_info(con, &cinfo2))
            status_start_row = cinfo2.cursor_y;

        for (int i = 0; i < count && i < 26; i++) {
            char content[256];
            /* Use shorter mount name field (12 chars instead of 30) */
            snprintf(content, sizeof(content), "  [%-12.12s] (starting...)", mounts[i]);
            char line[512];
            /* Truncate to console width */
            int len = (int)strlen(content);
            if (len > con_width) len = con_width;
            snprintf(line, sizeof(line), "%*.*s\n", len, len, content);
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
            /* Use atomic read for is_done to avoid race condition */
            if (platform_atomic_read(&statuses[i].is_done)) continue;
            all_done = false;

            /* Check if this mount made progress since last iteration */
            /* Use atomic read for dir_count */
            LONG current_count = platform_atomic_read(&statuses[i].dir_count);
            if (current_count != prev_dir_count[i]) {
                /* Activity detected - update last_active_ms */
                prev_dir_count[i] = current_count;
                statuses[i].last_active_ms = current_time;
            }
        }

        /* Check for inactivity timeout per mount */
        bool any_timed_out = false;
        for (int i = 0; i < count && i < 26; i++) {
            /* Use atomic read for is_done */
            if (platform_atomic_read(&statuses[i].is_done)) continue;
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
                /* Calculate available space for path */
                char mount_label[16];
                snprintf(mount_label, sizeof(mount_label), "%-12.12s", mounts[i]);
                
                /* Use atomic read for is_done */
                if (platform_atomic_read(&s->is_done)) {
                    snprintf(content, sizeof(content),
                             "  [%s] COMPLETE (%d directories)",
                             mount_label, s->final_count);
                } else if (!platform_atomic_read(&s->is_done) &&
                           (current_time - s->last_active_ms) >= timeout_ms) {
                    snprintf(content, sizeof(content),
                             "  [%s] WARNING: scan timed out (inactive)",
                             mount_label);
                } else {
                    const char *path = s->current_path[0] ? s->current_path : "(starting...)";
                    char path_buf[256];
                    
                    /* Truncate path to fit safely in content buffer */
                    size_t path_len = strlen(path);
                    if (path_len > sizeof(path_buf) - 1) {
                        /* Show end of path with ellipsis */
                        snprintf(path_buf, sizeof(path_buf), "...%s",
                                 path + path_len - sizeof(path_buf) + 4);
                    } else {
                        snprintf(path_buf, sizeof(path_buf), "%s", path);
                    }
                    
                    /* Use atomic read for dir_count */
                    snprintf(content, sizeof(content),
                             "  [%s] %5ld scanned  %s",
                             mount_label,
                             (long)platform_atomic_read(&s->dir_count),
                             path_buf);
                }
                /* Truncate content to console width to prevent line wrapping */
                int content_len = (int)strlen(content);
                if (content_len > con_width) {
                    content[con_width] = '\0';
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

/* ============================================================ iterative scan helpers */

/* Scan frame for iterative directory traversal - prevents stack overflow */
typedef struct ScanFrame {
    char path[MAX_PATH];
    int32_t parent_id;
#if NCD_PLATFORM_WINDOWS
    HANDLE find_handle;
    WIN32_FIND_DATAA find_data;
    bool find_data_valid;  /* true if find_data has valid entry */
#else
    DIR *dir;
#endif
} ScanFrame;

#define SCAN_STACK_INITIAL 1024
#define SCAN_STACK_MAX     65536  /* Prevent runaway allocation */

/* ============================================================ platform-specific scan implementations */

#if NCD_PLATFORM_WINDOWS

#include <windows.h>

static int platform_scan_directory(ScanCtx *ctx, const char *mount_path, int32_t parent_id)
{
    /* Allocate stack on heap to prevent stack overflow with deeply nested dirs */
    ScanFrame *stack = malloc(sizeof(ScanFrame) * SCAN_STACK_INITIAL);
    if (!stack) return 0;
    
    int stack_cap = SCAN_STACK_INITIAL;
    int stack_top = 0;
    int dir_count = 0;
    
    /* Initialize first frame */
    snprintf(stack[0].path, MAX_PATH, "%s", mount_path);
    stack[0].parent_id = parent_id;
    stack[0].find_handle = INVALID_HANDLE_VALUE;
    stack[0].find_data_valid = false;
    
    while (stack_top >= 0) {
        ScanFrame *frame = &stack[stack_top];
        
        /* Open directory if not already open */
        if (frame->find_handle == INVALID_HANDLE_VALUE) {
            char pattern[MAX_PATH];
            size_t plen = strlen(frame->path);
            bool needs_sep = (plen > 0 && frame->path[plen - 1] != '\\');
            if (snprintf(pattern, sizeof(pattern), "%s%s*", frame->path,
                         needs_sep ? "\\" : "") >= (int)sizeof(pattern)) {
                /* Path too long, pop frame */
                stack_top--;
                continue;
            }
            
            frame->find_handle = FindFirstFileExA(
                pattern, FindExInfoBasic, &frame->find_data,
                FindExSearchLimitToDirectories, NULL,
                FIND_FIRST_EX_LARGE_FETCH);
            
            if (frame->find_handle == INVALID_HANDLE_VALUE) {
                stack_top--;
                continue;
            }
            frame->find_data_valid = true;
        }
        
        /* Process current entry */
        if (frame->find_data_valid) {
            WIN32_FIND_DATAA *fd = &frame->find_data;
            frame->find_data_valid = false;  /* Will get next after processing */
            
            /* Skip non-directories and special entries */
            if (!(fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if (fd->cFileName[0] == '.' && 
                (fd->cFileName[1] == '\0' ||
                 (fd->cFileName[1] == '.' && fd->cFileName[2] == '\0')))
                continue;
            if (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;
            
            bool is_hidden = (fd->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
            bool is_system = (fd->dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
            
            if (is_hidden && !ctx->include_hidden) continue;
            if (is_system && !ctx->include_system) continue;
            
            /* Add directory entry */
            int my_id = db_add_dir(ctx->drv, fd->cFileName, frame->parent_id, 
                                   is_hidden, is_system);
            dir_count++;
            ctx->dir_count++;
            
            /* Build child path */
            char child[MAX_PATH];
            size_t mlen = strlen(frame->path);
            bool needs_sep = (mlen > 0 && frame->path[mlen - 1] != '\\');
            if (snprintf(child, sizeof(child), "%s%s%s", frame->path,
                         needs_sep ? "\\" : "", fd->cFileName) >= (int)sizeof(child)) {
                continue;  /* Path too long, skip */
            }
            
            /* Update progress */
            platform_atomic_inc(&ctx->status->dir_count);
            snprintf(ctx->status->current_path, sizeof(ctx->status->current_path), "%s", child);
            if (ctx->progress_fn && (ctx->dir_count % PROGRESS_INTERVAL) == 1)
                ctx->progress_fn(ctx->drv->letter, child, ctx->progress_user_data);
            
            /* Push child frame */
            if (stack_top + 1 >= stack_cap) {
                if (stack_cap >= SCAN_STACK_MAX) {
                    fprintf(stderr, "NCD: Directory nesting too deep: %s\n", child);
                    continue;
                }
                int new_cap = stack_cap * 2;
                if (new_cap > SCAN_STACK_MAX) new_cap = SCAN_STACK_MAX;
                
                ScanFrame *new_stack = realloc(stack, sizeof(ScanFrame) * new_cap);
                if (!new_stack) {
                    fprintf(stderr, "NCD: Out of memory during scan\n");
                    break;
                }
                stack = new_stack;
                stack_cap = new_cap;
            }
            
            ScanFrame *child_frame = &stack[++stack_top];
            snprintf(child_frame->path, MAX_PATH, "%s", child);
            child_frame->parent_id = my_id;
            child_frame->find_handle = INVALID_HANDLE_VALUE;
            child_frame->find_data_valid = false;
            continue;  /* Process child frame next */
        }
        
        /* Get next entry in current directory */
        if (!FindNextFileA(frame->find_handle, &frame->find_data)) {
            /* No more entries, close and pop */
            FindClose(frame->find_handle);
            stack_top--;
        } else {
            frame->find_data_valid = true;
        }
    }
    
    free(stack);
    return dir_count;
}

/* ================================================================ subdirectory scan */

/*
 * Scan a subdirectory and add it to the database.
 * Simple implementation: just scan and add entries without removing old ones.
 * Returns number of directories added, or -1 on error.
 */
int scan_subdirectory(NcdDatabase   *db,
                      char           drive_letter,
                      const char    *subdir_path,
                      bool           include_hidden,
                      bool           include_system)
{
    if (!subdir_path || !subdir_path[0]) return -1;
    
    /* Find or create the drive */
    DriveData *drv = NULL;
    for (int i = 0; i < db->drive_count; i++) {
        if (db->drives[i].letter == drive_letter) {
            drv = &db->drives[i];
            break;
        }
    }
    
    if (!drv) {
        drv = db_add_drive(db, drive_letter);
#if NCD_PLATFORM_WINDOWS
        drv->type = DRIVE_FIXED;
        char mount_path[MAX_PATH];
        snprintf(mount_path, sizeof(mount_path), "%c:\\", drive_letter);
        platform_strncpy_s(drv->label, sizeof(drv->label), mount_path);
#else
        drv->type = 0;
        char mount_path[MAX_PATH];
        snprintf(mount_path, sizeof(mount_path), "/mnt/%c", tolower((unsigned char)drive_letter));
        platform_strncpy_s(drv->label, sizeof(drv->label), mount_path);
#endif
    }
    
    /* Normalize path */
    char norm_path[MAX_PATH];
    platform_strncpy_s(norm_path, sizeof(norm_path), subdir_path);
    path_normalize_separators(norm_path);
    
    /* Get the directory name */
    const char *subdir_name = path_leaf(norm_path);
    
    /* Add the subdirectory entry */
    int subdir_id = db_add_dir(drv, subdir_name, -1, false, false);
    
    /* Create scan context */
    ScanCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.drv = drv;
    ctx.include_hidden = include_hidden;
    ctx.include_system = include_system;
    ctx.dir_count = 0;
#if NCD_PLATFORM_LINUX
    ctx.visited = NULL;
#endif
    
    /* Scan */
    return platform_scan_directory(&ctx, norm_path, subdir_id);
}

#elif NCD_PLATFORM_LINUX

#include <sys/stat.h>
#include <unistd.h>

static int platform_scan_directory(ScanCtx *ctx, const char *mount_path, int32_t parent_id)
{
    /* Allocate stack on heap to prevent stack overflow with deeply nested dirs */
    ScanFrame *stack = malloc(sizeof(ScanFrame) * SCAN_STACK_INITIAL);
    if (!stack) return 0;
    
    int stack_cap = SCAN_STACK_INITIAL;
    int stack_top = 0;
    int dir_count = 0;
    
    /* Initialize first frame */
    snprintf(stack[0].path, MAX_PATH, "%s", mount_path);
    stack[0].parent_id = parent_id;
    stack[0].dir = NULL;
    
    while (stack_top >= 0) {
        ScanFrame *frame = &stack[stack_top];
        
        /* Open directory if not already open */
        if (frame->dir == NULL) {
            /* When scanning root (/), only scan whitelisted system directories */
            if (stack_top > 0 && strcmp(frame->path, "/") == 0) {
                /* Only the initial mount should be "/" */
                stack_top--;
                continue;
            }
            
            frame->dir = opendir(frame->path);
            if (frame->dir == NULL) {
                stack_top--;
                continue;
            }
        }
        
        /* Read next entry */
        struct dirent *ent = readdir(frame->dir);
        if (ent == NULL) {
            /* No more entries, close and pop */
            closedir(frame->dir);
            stack_top--;
            continue;
        }
        
        /* Skip special entries */
        if (ent->d_name[0] == '.' && 
            (ent->d_name[1] == '\0' ||
             (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;
        
        /* Build child path */
        char child[MAX_PATH];
        size_t mlen = strlen(frame->path);
        bool needs_sep = (mlen == 0) || (frame->path[mlen - 1] != '/');
        if (snprintf(child, sizeof(child), "%s%s%s", frame->path,
                     needs_sep ? "/" : "", ent->d_name) >= (int)sizeof(child))
            continue;
        
        /* Get file info */
        struct stat st;
        if (lstat(child, &st) != 0)
            continue;
        if (!S_ISDIR(st.st_mode))
            continue;
        
        /* Check for bind mount cycles using device+inode */
        if (ctx->visited) {
            DirId id = {st.st_dev, st.st_ino};
            if (diridset_contains(ctx->visited, id)) {
                /* Cycle detected - skip this directory */
                continue;
            }
            diridset_add(ctx->visited, id);
        }
        
        bool is_hidden = (ent->d_name[0] == '.');
        bool is_system = false;
        
        if (is_hidden && !ctx->include_hidden)
            continue;
        
        /* Add directory entry */
        int my_id = db_add_dir(ctx->drv, ent->d_name, frame->parent_id, 
                               is_hidden, is_system);
        dir_count++;
        ctx->dir_count++;
        
        /* Update progress */
        platform_atomic_inc(&ctx->status->dir_count);
        snprintf(ctx->status->current_path, sizeof(ctx->status->current_path), "%s", child);
        if (ctx->progress_fn && (ctx->dir_count % PROGRESS_INTERVAL) == 1)
            ctx->progress_fn(ctx->drv->letter, child, ctx->progress_user_data);
        
        /* Push child frame */
        if (stack_top + 1 >= stack_cap) {
            if (stack_cap >= SCAN_STACK_MAX) {
                fprintf(stderr, "NCD: Directory nesting too deep: %s\n", child);
                continue;
            }
            int new_cap = stack_cap * 2;
            if (new_cap > SCAN_STACK_MAX) new_cap = SCAN_STACK_MAX;
            
            ScanFrame *new_stack = realloc(stack, sizeof(ScanFrame) * new_cap);
            if (!new_stack) {
                fprintf(stderr, "NCD: Out of memory during scan\n");
                break;
            }
            stack = new_stack;
            stack_cap = new_cap;
        }
        
        ScanFrame *child_frame = &stack[++stack_top];
        snprintf(child_frame->path, MAX_PATH, "%s", child);
        child_frame->parent_id = my_id;
        child_frame->dir = NULL;
    }
    
    free(stack);
    return dir_count;
}

#else
#error Unsupported platform in scanner.c
#endif
