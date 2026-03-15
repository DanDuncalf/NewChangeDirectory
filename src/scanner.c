/*
 * scanner.c  --  Drive/mount enumeration and recursive directory scan
 *
 * Windows: one thread per drive, FindFirstFileExA, Win32 console display.
 * Linux:   one thread per mount point, opendir/readdir/lstat, ANSI display.
 */

#include "scanner.h"
#include "database.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#if NCD_PLATFORM_WINDOWS

/* ============================================================= con_write  */
/*
 * Write directly to CONOUT$ -- bypasses all CRT stdout/stderr buffering.
 * Safe to call from any thread.  Used for progress/status messages only.
 */
/*
 * Return the single shared CONOUT$ handle, opening it on first call.
 *
 * con_handle() is always first called from the main thread (the
 * "Scanning drives:" message) before any worker thread is spawned, so the
 * static initialisation has no race.  After that the handle is read-only
 * from threads.  This replaces per-call CreateFile/CloseHandle (which
 * fired once every PROGRESS_INTERVAL directories).
 */
static HANDLE con_handle(void)
{
    static HANDLE h = INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileA("CONOUT$",
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, 0, NULL);
    return h;
}

static void con_write(const char *s)
{
    HANDLE h = con_handle();
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteConsoleA(h, s, (DWORD)strlen(s), &w, NULL);
    } else {
        fputs(s, stdout);
        fflush(stdout);
    }
}

static void con_writef(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    con_write(buf);
}

/*
 * Write one status line at a fixed absolute buffer row without moving
 * the visible console window. This prevents redraw jitter/snap.
 */
static void con_write_line_at(HANDLE con, SHORT row, int width, const char *content)
{
    if (con == INVALID_HANDLE_VALUE) return;
    if (width < 1) return;

    COORD pos = { 0, row };
    DWORD written = 0;

    FillConsoleOutputCharacterA(con, ' ', (DWORD)width, pos, &written);
    FillConsoleOutputAttribute(con,
                               FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
                               (DWORD)width, pos, &written);

    char line[512];
    snprintf(line, sizeof(line), "%-*.*s", width, width, content);
    WriteConsoleOutputCharacterA(con, line, (DWORD)strlen(line), pos, &written);
}

/* ================================================================ tuning  */

/* Fire the progress callback after every Nth directory found per drive. */
#define PROGRESS_INTERVAL  100

/*
 * Maximum milliseconds to wait for ALL drive threads to finish.
 * A hung network drive can block GetVolumeInformationA for 30-120 s;
 * threads that don't finish within this window are abandoned (the drive
 * is simply omitted from the database for this scan cycle).
 *
 * 5 minutes gives large spindle media drives enough time to enumerate
 * their full directory tree without being cut off prematurely.
 */
#define SCAN_TIMEOUT_MS  300000  /* 5 minutes */

/* ================================================================ helpers */

static const char *SKIP_NAMES[] = {
    ".", "..",
    "$Recycle.Bin",
    "System Volume Information",
    "Recovery",
    NULL
};

static bool should_skip_name(const char *name)
{
    for (int i = 0; SKIP_NAMES[i]; i++)
        if (_stricmp(name, SKIP_NAMES[i]) == 0) return true;
    return false;
}

/* ======================================================= recursive scan   */

/*
 * Per-drive context passed down the recursive call stack.
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

/*
 * Recursive walker.  base is the directory to enumerate, e.g. "C:\Users".
 * scan_directory appends "\*" internally to build the search pattern.
 */
static void scan_directory(ScanCtx    *ctx,
                            const char *base,
                            int32_t     parent_id)
{
    /* Build search pattern. */
    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*", base) >= (int)sizeof(pattern))
        return;   /* path too long -- skip silently */

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileExA(
                   pattern,
                   FindExInfoBasic,                /* no 8.3 short names      */
                   &fd,
                   FindExSearchLimitToDirectories, /* dirs only (NTFS honours)*/
                   NULL,
                   FIND_FIRST_EX_LARGE_FETCH);     /* fewer kernel round-trips*/

    if (h == INVALID_HANDLE_VALUE) return;

    do {
        /* FindExSearchLimitToDirectories is a hint -- double-check. */
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))  continue;
        if (should_skip_name(fd.cFileName))                     continue;
        /* Skip reparse points (junctions / symlinks) to avoid loops. */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        bool is_hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        bool is_system = (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)  != 0;
        if (is_hidden && !ctx->include_hidden) continue;
        if (is_system && !ctx->include_system) continue;

        /* Register in the database. */
        int my_id = db_add_dir(ctx->drv, fd.cFileName, parent_id,
                               is_hidden, is_system);
        ctx->dir_count++;

        /* Build full child path. */
        char child[MAX_PATH];
        if (snprintf(child, sizeof(child), "%s\\%s", base, fd.cFileName)
                >= (int)sizeof(child))
            continue;   /* path too long -- skip this subtree */

        /* Update shared status so the main-thread display loop can read it.
         * dir_count is incremented atomically; current_path is updated every
         * PROGRESS_INTERVAL dirs (a racy read is acceptable for display). */
        InterlockedIncrement(&ctx->status->dir_count);
        if ((ctx->dir_count % PROGRESS_INTERVAL) == 1) {
            snprintf(ctx->status->current_path,
                     sizeof(ctx->status->current_path), "%s", child);
            if (ctx->progress_fn)
                ctx->progress_fn(ctx->drv->letter, child, ctx->progress_user_data);
        }

        /* Recurse into child directory. */
        scan_directory(ctx, child, (int32_t)my_id);

    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

/* =========================================================== threading    */

/*
 * Per-drive thread data block.
 * drive_slot is pre-assigned before any thread starts, so threads never
 * modify db->drives[] -- only the DriveData at their assigned slot.
 * status points into the scan_all_drives stack-allocated DriveStatus array;
 * the main thread reads it every 100 ms for the live display.
 */
typedef struct {
    NcdDatabase *db;
    int          drive_slot;     /* index into db->drives[]                 */
    bool         include_hidden;
    bool         include_system;
    DriveStatus *status;         /* shared with main-thread display loop    */
    int          dirs_found;     /* written on completion                   */
} ScanThreadData;

static DWORD WINAPI drive_thread(LPVOID param)
{
    ScanThreadData *td  = (ScanThreadData *)param;
    DriveData      *drv = &td->db->drives[td->drive_slot];

    /*
     * GetVolumeInformationA can block for a long time on a slow or
     * unreachable network share.  We call it with full error awareness:
     * if it fails (returns FALSE) we simply leave the label empty and
     * continue -- the directory scan itself may also time out, but at
     * least we don't hang here for minutes before starting.
     */
    char root[4] = { drv->letter, ':', '\\', '\0' };
    char label[64] = {0};
    GetVolumeInformationA(root, label, sizeof(label),
                          NULL, NULL, NULL, NULL, 0);
    /* If the call failed, label[] is still all-zero -- that's fine. */
    strncpy(drv->label, label, sizeof(drv->label) - 1);

    /* Build scan context. */
    ScanCtx ctx = {
        .drv            = drv,
        .include_hidden = td->include_hidden,
        .include_system = td->include_system,
        .status         = td->status,
        .dir_count      = 0,
    };

    /* Root base: "C:" -- scan_directory will append "\*" */
    char base[3] = { drv->letter, ':', '\0' };
    scan_directory(&ctx, base, -1);

    /* Signal completion to the main-thread display loop. */
    td->status->final_count = ctx.dir_count;
    InterlockedExchange(&td->status->is_done, 1);

    td->dirs_found = ctx.dir_count;
    return 0;
}

/* ================================================================ public   */

int scan_drive(NcdDatabase   *db,
               char           drive_letter,
               bool           include_hidden,
               bool           include_system,
               ScanProgressFn progress_fn,
               void          *user_data)
{
    drive_letter = (char)toupper((unsigned char)drive_letter);

    char root[4] = { drive_letter, ':', '\\', '\0' };
    UINT dtype = GetDriveTypeA(root);
    if (dtype == DRIVE_NO_ROOT_DIR || dtype == DRIVE_UNKNOWN) return -1;
    if (dtype == DRIVE_CDROM) return 0;

    /* Remove stale data for this drive if re-scanning. */
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
    drv->type = dtype;

    char label[64] = {0};
    GetVolumeInformationA(root, label, sizeof(label),
                          NULL, NULL, NULL, NULL, 0);
    strncpy(drv->label, label, sizeof(drv->label) - 1);

    /* Single-drive path: call the walker directly without threading. */

    DriveStatus local_status;
    memset(&local_status, 0, sizeof(local_status));
    local_status.drive_letter = drive_letter;

    ScanCtx ctx = {
        .drv            = drv,
        .include_hidden = include_hidden,
        .include_system = include_system,
        .status         = &local_status,
        .dir_count      = 0,
        .progress_fn    = progress_fn,
        .progress_user_data = user_data,
    };
    char base[3] = { drive_letter, ':', '\0' };
    scan_directory(&ctx, base, -1);

    return drv->dir_count;
}

int scan_all_drives(NcdDatabase *db,
                    bool         include_hidden,
                    bool         include_system)
{
    /* ---------------------------------- enumerate valid drives            */
    char letters[26];
    UINT types[26];
    int  ndrvs = 0;

    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue;
        char letter = (char)('A' + i);
        char root[4] = { letter, ':', '\\', '\0' };
        UINT dtype = GetDriveTypeA(root);
        if (dtype == DRIVE_NO_ROOT_DIR || dtype == DRIVE_UNKNOWN ||
            dtype == DRIVE_CDROM) continue;
        letters[ndrvs] = letter;
        types[ndrvs]   = dtype;
        ndrvs++;
    }

    if (ndrvs == 0) return 0;

    /* ---------------------------------- pre-allocate DriveData slots
     * All slots must exist before any thread starts so that db->drives[]
     * is never reallocated while a thread holds a pointer into it.        */
    int slots[26];
    for (int i = 0; i < ndrvs; i++) {
        DriveData *drv = db_add_drive(db, letters[i]);
        drv->type      = types[i];
        slots[i]       = db->drive_count - 1;
    }

    /* ---------------------------------- per-drive status (stack-allocated)
     * The display loop reads these every 100 ms; workers write them.      */
    DriveStatus statuses[26];
    memset(statuses, 0, sizeof(statuses));
    for (int i = 0; i < ndrvs; i++)
        statuses[i].drive_letter = letters[i];

    /* ---------------------------------- console setup (BEFORE spawning)
     * The status block must be drawn before any thread starts.  If we
     * spawn threads first, fast SSD drives can complete their entire scan
     * before the display loop runs, so the user would never see them as
     * "actively scanning" -- they'd only ever appear as COMPLETE.
     * Drawing the initial "(starting...)" frame first guarantees every
     * drive is visible from the very first moment.                        */
    HANDLE con      = con_handle();
    bool   has_con  = (con != INVALID_HANDLE_VALUE);
    int    con_width = 79;   /* safe default if query fails                 */
    COORD  status_start = {0, 0};

    if (has_con) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(con, &csbi)) {
            con_width    = csbi.dwSize.X > 2 ? csbi.dwSize.X - 2 : 79;
            if (con_width > 200) con_width = 200;  /* sanity cap           */
        }
        /* Print header listing all drives to be scanned. */
        {
            char drvlist[128] = "  Scanning drives:";
            for (int i = 0; i < ndrvs; i++) {
                char tmp[5];
                snprintf(tmp, sizeof(tmp), " %c:", letters[i]);
                strncat(drvlist, tmp, sizeof(drvlist) - strlen(drvlist) - 1);
            }
            strncat(drvlist, "\r\n", sizeof(drvlist) - strlen(drvlist) - 1);
            con_write(drvlist);
        }
        /* Capture cursor position -- this is row 0 of the status block.  */
        CONSOLE_SCREEN_BUFFER_INFO csbi2;
        if (GetConsoleScreenBufferInfo(con, &csbi2))
            status_start = csbi2.dwCursorPosition;

        /* Draw one "(starting...)" line per drive so all drives are
         * visible before any thread has had a chance to complete.         */
        for (int i = 0; i < ndrvs; i++) {
            char content[256];
            snprintf(content, sizeof(content),
                     "  [%c:] (starting...)", letters[i]);
            char line[256];
            snprintf(line, sizeof(line), "%-*.*s\r\n",
                     con_width, con_width, content);
            DWORD w;
            WriteConsoleA(con, line, (DWORD)strlen(line), &w, NULL);
        }
    }

    /* ---------------------------------- spawn one thread per drive        */
    ScanThreadData td[26];
    HANDLE         handles[26];
    int            nhandles = 0;

    for (int i = 0; i < ndrvs; i++) {
        td[i].db             = db;
        td[i].drive_slot     = slots[i];
        td[i].include_hidden = include_hidden;
        td[i].include_system = include_system;
        td[i].status         = &statuses[i];
        td[i].dirs_found     = 0;

        handles[nhandles] = CreateThread(NULL, 0, drive_thread, &td[i], 0, NULL);
        if (handles[nhandles]) {
            nhandles++;
        } else {
            /* Thread creation failed -- run inline as fallback.
             * drive_thread sets is_done before returning, so the display
             * loop will show COMPLETE for this drive immediately.
             * No console message here -- it would corrupt the status block
             * that was already drawn above.                                */
            drive_thread(&td[i]);
        }
    }

    /* ---------------------------------- 100 ms display loop              */
    DWORD deadline = GetTickCount() + SCAN_TIMEOUT_MS;

    for (;;) {
        /* Check completion and timeout. */
        bool all_done = true;
        for (int i = 0; i < ndrvs; i++)
            if (!statuses[i].is_done) { all_done = false; break; }

        bool timed_out = !all_done &&
                         (GetTickCount() >= deadline);

        /* Draw current status.
         *
         * Mid-scan: only update in-place when the status block is within
         * the current viewport.  Calling SetConsoleCursorPosition + Write-
         * ConsoleA at a position above the visible window causes the console
         * to snap the window back to that position on every 100 ms tick,
         * making it impossible to scroll away while scanning.  By checking
         * srWindow first we skip the redraw when the user has scrolled
         * elsewhere, leaving the view undisturbed.
         *
         * Final update (all_done / timed_out): always redraw regardless of
         * scroll position so the terminal is left showing the completed
         * state before the caller prints the summary line.               */
        if (has_con) {
            bool in_view = (all_done || timed_out);   /* final: always show */
            if (!in_view) {
                CONSOLE_SCREEN_BUFFER_INFO vp;
                if (GetConsoleScreenBufferInfo(con, &vp))
                    in_view = (status_start.Y >= vp.srWindow.Top &&
                               status_start.Y + ndrvs <= vp.srWindow.Bottom);
            }
            if (in_view) {
                for (int i = 0; i < ndrvs; i++) {
                    DriveStatus *s = &statuses[i];
                    char content[256];
                    if (s->is_done) {
                        snprintf(content, sizeof(content),
                                 "  [%c:] COMPLETE (%d directories)",
                                 s->drive_letter, s->final_count);
                    } else if (timed_out) {
                        snprintf(content, sizeof(content),
                                 "  [%c:] WARNING: scan timed out -- drive skipped",
                                 s->drive_letter);
                    } else {
                        snprintf(content, sizeof(content),
                                 "  [%c:] %5ld scanned  %s",
                                 s->drive_letter,
                                 (long)s->dir_count,
                                 s->current_path[0] ? s->current_path
                                                    : "(starting...)");
                    }
                    con_write_line_at(con, (SHORT)(status_start.Y + i), con_width, content);
                }
            }
        }

        if (all_done || timed_out) break;
        Sleep(100);
    }

    /* Move console cursor past the status block. */
    if (has_con) {
        COORD end = { 0, (SHORT)(status_start.Y + ndrvs) };
        SetConsoleCursorPosition(con, end);
    }

    /* ---------------------------------- close thread handles              */
    for (int j = 0; j < nhandles; j++) {
        if (handles[j]) {
            /* Should already be signalled; 0 ms wait just to be safe. */
            WaitForSingleObject(handles[j], 0);
            CloseHandle(handles[j]);
        }
    }

    /* ---------------------------------- total                             */
    int total = 0;
    for (int i = 0; i < ndrvs; i++)
        total += td[i].dirs_found;

    return total;
}

/* ============================================================== end Windows */
#elif NCD_PLATFORM_LINUX

#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <termios.h>

/* ================================================================ tuning  */
#define PROGRESS_INTERVAL  100
#define SCAN_TIMEOUT_MS    300000  /* 5 minutes */

/* ================================================================ helpers */

static const char *SKIP_NAMES_L[] = { ".", "..", NULL };

static bool should_skip_name_linux(const char *name)
{
    for (int i = 0; SKIP_NAMES_L[i]; i++)
        if (strcmp(name, SKIP_NAMES_L[i]) == 0) return true;
    return false;
}

/*
 * Filesystem types considered pseudo / virtual -- skip these mount points.
 * Any type not in this list but reported by /proc/mounts will be scanned.
 */
static const char *PSEUDO_FS[] = {
    "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "securityfs",
    "cgroup", "cgroup2", "pstore", "bpf", "autofs", "hugetlbfs",
    "mqueue", "debugfs", "tracefs", "ramfs", "squashfs", "overlay",
    "fusectl", "nsfs", "efivarfs", "configfs", "selinuxfs", "pipefs",
    "sockfs", "rpc_pipefs", "nfsd", "sunrpc", "cpuset", "xenfs",
    "fuse.portal", "fuse.gvfsd-fuse",
    NULL
};

/* Return true when running inside WSL (any version).
 * WSL's virtio-FS I/O gateway is single-threaded; parallel directory scans
 * all block behind each other, so sequential scanning gives better display
 * feedback and the same overall throughput.                                */
static bool detect_wsl(void)
{
    static int cached = -1;
    if (cached >= 0) return (bool)cached;
    FILE *f = fopen("/proc/version", "r");
    if (!f) { cached = 0; return false; }
    char buf[256] = {0};
    bool wsl = false;
    if (fgets(buf, sizeof(buf), f))
        wsl = (strstr(buf, "Microsoft") != NULL || strstr(buf, "WSL") != NULL);
    fclose(f);
    cached = wsl ? 1 : 0;
    return wsl;
}

static bool is_pseudo_fs(const char *fstype)
{
    for (int i = 0; PSEUDO_FS[i]; i++)
        if (strcmp(fstype, PSEUDO_FS[i]) == 0) return true;
    return false;
}

/* ============================================================== con_write */

static int linux_con_fd(void)
{
    static int fd = -2;
    if (fd == -2) fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    return fd;
}

/* write() wrapper that discards the return value without -Wunused-result */
static void tty_write_l(int fd, const void *buf, size_t n)
{
    ssize_t r = write(fd, buf, n); (void)r;
}


/* Query cursor row via ANSI CPR (ESC [ 6 n).  Returns 0 on failure.
 *
 * The tty must be in raw mode to receive the response: in canonical mode
 * the terminal line-buffers input, so the ESC[row;colR reply never becomes
 * available to read() and also gets echoed as literal text on the screen.
 * We save/restore the terminal attributes around the query.              */
static int get_cursor_row_linux(int fd)
{
    if (fd < 0) return 0;

    /* Switch to raw mode (no echo, no canonical line-buffering). */
    struct termios old_tio, raw_tio;
    bool have_tio = (tcgetattr(fd, &old_tio) == 0);
    if (have_tio) {
        raw_tio = old_tio;
        raw_tio.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHOE | ECHOK |
                                        ECHONL | ISIG);
        raw_tio.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
        raw_tio.c_cc[VMIN]  = 1;
        raw_tio.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSANOW, &raw_tio);
    }

    /* Send the CPR query. */
    ssize_t wn = write(fd, "\x1b[6n", 4);
    if (wn != 4) {
        if (have_tio) tcsetattr(fd, TCSANOW, &old_tio);
        return 0;
    }

    /* Read the response: ESC [ row ; col R  (1-based). */
    char resp[32] = {0};
    int  i = 0;
    while (i < (int)sizeof(resp) - 1) {
        fd_set rfds;
        struct timeval tv = {0, 500000};   /* 0.5 s */
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) break;
        if (read(fd, &resp[i], 1) <= 0) break;
        if (resp[i++] == 'R') break;
    }

    /* Restore terminal mode before parsing (parsing cannot fail). */
    if (have_tio) tcsetattr(fd, TCSANOW, &old_tio);

    int row = 0, col = 0;
    if (sscanf(resp, "\x1b[%d;%dR", &row, &col) < 1)
        sscanf(resp, "[%d;%dR", &row, &col);
    return (row > 0) ? row - 1 : 0;   /* convert to 0-based */
}

/* Overwrite a single status line at the given absolute row.               */
static void con_write_line_at_linux(int fd, int row, int width, const char *content)
{
    if (fd < 0 || width < 1) return;
    char seq[32];
    snprintf(seq, sizeof(seq), "\x1b[%d;1H\x1b[2K", row + 1);
    tty_write_l(fd, seq, strlen(seq));

    char line[512];
    snprintf(line, sizeof(line), "%-*.*s", width, width, content);
    tty_write_l(fd, line, strlen(line));
}

/* Progress callback used by the sequential-scan paths.
 * user_data must point to a LinuxSeqProgress struct.                      */
typedef struct {
    int  con;
    int  row;          /* absolute terminal row for this drive's status line */
    int  con_width;
    char mnt_label[24];
    long approx_count; /* incremented by PROGRESS_INTERVAL each callback    */
} LinuxSeqProgress;

static void linux_seq_progress(char dl, const char *path, void *ud)
{
    (void)dl;
    LinuxSeqProgress *p = (LinuxSeqProgress *)ud;
    p->approx_count += PROGRESS_INTERVAL;
    char content[256];
    snprintf(content, sizeof(content),
             "  [%-20.20s] %5ld scanned  %.*s",
             p->mnt_label, p->approx_count,
             (int)(sizeof(content) - 42), path ? path : "");
    con_write_line_at_linux(p->con, p->row, p->con_width, content);
}

/* ======================================================= recursive scan  */

typedef struct {
    DriveData      *drv;
    bool            include_hidden;
    bool            include_system;
    DriveStatus    *status;
    int             dir_count;
    dev_t           root_dev;       /* don't cross mount boundaries */
    ScanProgressFn  progress_fn;
    void           *progress_user_data;
} ScanCtxL;

static void scan_directory_linux(ScanCtxL *ctx, const char *base, int32_t parent_id)
{
    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (should_skip_name_linux(ent->d_name)) continue;

        char child[MAX_PATH];
        if (snprintf(child, sizeof(child), "%s/%s", base, ent->d_name)
                >= (int)sizeof(child))
            continue;

        struct stat st;
        if (lstat(child, &st) != 0)          continue;
        if (!S_ISDIR(st.st_mode))             continue;
        if (st.st_dev != ctx->root_dev)       continue;  /* mount crossing */

        bool is_hidden = (ent->d_name[0] == '.');
        bool is_system = false;   /* Linux has no system attribute */
        if (is_hidden && !ctx->include_hidden) continue;

        int my_id = db_add_dir(ctx->drv, ent->d_name, parent_id, is_hidden, is_system);
        platform_atomic_inc(&ctx->status->dir_count);
        ctx->dir_count++;

        if ((ctx->dir_count % PROGRESS_INTERVAL) == 1) {
            snprintf(ctx->status->current_path,
                     sizeof(ctx->status->current_path), "%s", child);
            if (ctx->progress_fn)
                ctx->progress_fn(ctx->drv->letter, child, ctx->progress_user_data);
        }

        scan_directory_linux(ctx, child, (int32_t)my_id);
    }

    closedir(d);
}

/* ======================================================= threading       */

typedef struct {
    NcdDatabase *db;
    int          drive_slot;
    bool         include_hidden;
    bool         include_system;
    DriveStatus *status;
    int          dirs_found;
} ScanThreadDataL;

static unsigned long linux_drive_thread(void *param)
{
    ScanThreadDataL *td  = (ScanThreadDataL *)param;
    DriveData       *drv = &td->db->drives[td->drive_slot];

    /* The mount point path is stored in drv->label by the caller */
    const char *mount = drv->label[0] ? drv->label : "/";

    struct stat root_st;
    if (stat(mount, &root_st) != 0) {
        platform_atomic_exchange(&td->status->is_done, 1);
        td->dirs_found = 0;
        return 0;
    }

    ScanCtxL ctx = {
        .drv            = drv,
        .include_hidden = td->include_hidden,
        .include_system = td->include_system,
        .status         = td->status,
        .dir_count      = 0,
        .root_dev       = root_st.st_dev,
    };

    scan_directory_linux(&ctx, mount, -1);

    td->status->final_count = ctx.dir_count;
    platform_atomic_exchange(&td->status->is_done, 1);
    td->dirs_found = ctx.dir_count;
    return 0;
}

/* ================================================================ public  */

int scan_drive(NcdDatabase   *db,
               char           drive_letter,
               bool           include_hidden,
               bool           include_system,
               ScanProgressFn progress_fn,
               void          *user_data)
{
    /*
     * On Linux, drive_letter is the mount-index byte (1..26) or an uppercase
     * WSL drive letter ('C'..'Z').  Find the DriveData and copy its label
     * into a local buffer BEFORE removing the entry from db->drives[] — the
     * memmove in the removal loop (and a possible realloc inside db_add_drive)
     * would otherwise leave `mount` pointing at stale memory.
     */
    char mount_buf[MAX_PATH] = "/";
    {
        DriveData *drv = db_find_drive(db, drive_letter);
        if (drv && drv->label[0])
            snprintf(mount_buf, sizeof(mount_buf), "%s", drv->label);
    }
    const char *mount = mount_buf;

    /* Remove stale data for this "drive" if re-scanning */
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

    DriveData *new_drv = db_add_drive(db, drive_letter);
    new_drv->type = PLATFORM_DRIVE_FIXED;
    snprintf(new_drv->label, sizeof(new_drv->label), "%s", mount);

    struct stat root_st;
    if (stat(mount, &root_st) != 0) return -1;

    DriveStatus local_status;
    memset(&local_status, 0, sizeof(local_status));
    local_status.drive_letter = drive_letter;

    ScanCtxL ctx = {
        .drv                = new_drv,
        .include_hidden     = include_hidden,
        .include_system     = include_system,
        .status             = &local_status,
        .dir_count          = 0,
        .root_dev           = root_st.st_dev,
        .progress_fn        = progress_fn,
        .progress_user_data = user_data,
    };
    scan_directory_linux(&ctx, mount, -1);
    return new_drv->dir_count;
}

int scan_all_drives(NcdDatabase *db, bool include_hidden, bool include_system)
{
    /* ---- enumerate real mount points from /proc/mounts ---------------  */
    char     mnt_points[26][MAX_PATH];
    char     mnt_letters[26];
    int      nmounts = 0;

    FILE *mf = fopen("/proc/mounts", "r");
    if (mf) {
        char line[1024];
        while (fgets(line, sizeof(line), mf) && nmounts < 26) {
            char device[256], mountpoint[MAX_PATH], fstype[64];
            if (sscanf(line, "%255s %4095s %63s", device, mountpoint, fstype) != 3)
                continue;
            if (is_pseudo_fs(fstype)) continue;

            /* Skip pseudo / kernel mount prefixes. */
            if (strncmp(mountpoint, "/proc",  5) == 0) continue;
            if (strncmp(mountpoint, "/sys",   4) == 0) continue;
            if (strncmp(mountpoint, "/dev",   4) == 0) continue;
            if (strncmp(mountpoint, "/run",   4) == 0) continue;

            snprintf(mnt_points[nmounts], MAX_PATH, "%s", mountpoint);

            /*
             * WSL drive-letter detection:
             * If the mountpoint is /mnt/X (exactly, single letter), treat X
             * as a Windows drive letter and assign it as the drive index so
             * that  ncd /r c-e  and  ncd downloads  work with drive letters.
             * This covers both WSL1 (drvfs) and WSL2 (9p / drvfs).
             */
            char assigned = (char)(nmounts + 1);  /* default: sequential 0x01..0x1A */
            if (mountpoint[0] == '/' &&
                mountpoint[1] == 'm' &&
                mountpoint[2] == 'n' &&
                mountpoint[3] == 't' &&
                mountpoint[4] == '/' &&
                isalpha((unsigned char)mountpoint[5]) &&
                mountpoint[6] == '\0') {
                assigned = (char)toupper((unsigned char)mountpoint[5]);
            }
            mnt_letters[nmounts] = assigned;
            nmounts++;
        }
        fclose(mf);
    }

    if (nmounts == 0) {
        /* Fallback: scan the entire root filesystem */
        strcpy(mnt_points[0], "/");
        mnt_letters[0] = (char)1;
        nmounts = 1;
    }

    /* ---- pre-allocate DriveData slots --------------------------------- */
    int slots[26];
    for (int i = 0; i < nmounts; i++) {
        DriveData *drv = db_add_drive(db, mnt_letters[i]);
        drv->type      = PLATFORM_DRIVE_FIXED;
        snprintf(drv->label, sizeof(drv->label), "%s", mnt_points[i]);
        slots[i]       = db->drive_count - 1;
    }

    /* ---- per-mount status (stack allocated) --------------------------- */
    DriveStatus statuses[26];
    memset(statuses, 0, sizeof(statuses));
    for (int i = 0; i < nmounts; i++)
        statuses[i].drive_letter = mnt_letters[i];

    /* ---- console setup ------------------------------------------------ */
    int  con      = linux_con_fd();
    bool has_con  = (con >= 0);
    int  con_width = 79;
    int  status_start = 0;

    if (has_con) {
        struct winsize ws;
        if (ioctl(con, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 2)
            con_width = ws.ws_col - 2;
        if (con_width > 200) con_width = 200;

        /* Print header listing all mount points */
        char drvlist[512] = "  Scanning mounts:";
        for (int i = 0; i < nmounts; i++) {
            /* Abbreviate mount path to 48 chars for the display header */
            char tmp[52];
            snprintf(tmp, sizeof(tmp), " %.*s", (int)(sizeof(tmp) - 2), mnt_points[i]);
            strncat(drvlist, tmp, sizeof(drvlist) - strlen(drvlist) - 1);
        }
        strncat(drvlist, "\n", sizeof(drvlist) - strlen(drvlist) - 1);
        tty_write_l(con, drvlist, strlen(drvlist));

        /* Record cursor row so we can redraw status in-place */
        status_start = get_cursor_row_linux(con);

        /* Draw initial "(starting...)" lines */
        for (int i = 0; i < nmounts; i++) {
            char content[256];
            snprintf(content, sizeof(content),
                     "  [%-20.20s] (starting...)", mnt_points[i]);
            char line[256];
            snprintf(line, sizeof(line), "%-*.*s\n", con_width, con_width, content);
            tty_write_l(con, line, strlen(line));
        }
    }

    /* ---- sequential mode (WSL: single I/O gateway, no benefit to threads) */
    if (detect_wsl()) {
        int total = 0;
        for (int i = 0; i < nmounts; i++) {
            if (has_con) {
                char content[256];
                snprintf(content, sizeof(content),
                         "  [%-20.20s] scanning...", mnt_points[i]);
                con_write_line_at_linux(con, status_start + i, con_width, content);
            }

            LinuxSeqProgress prog;
            prog.con          = con;
            prog.row          = status_start + i;
            prog.con_width    = con_width;
            prog.approx_count = 0;
            snprintf(prog.mnt_label, sizeof(prog.mnt_label), "%s", mnt_points[i]);

            int n = scan_drive(db, mnt_letters[i],
                               include_hidden, include_system,
                               has_con ? linux_seq_progress : NULL, &prog);

            if (has_con) {
                char content[256];
                if (n >= 0)
                    snprintf(content, sizeof(content),
                             "  [%-20.20s] COMPLETE (%d directories)",
                             mnt_points[i], n);
                else
                    snprintf(content, sizeof(content),
                             "  [%-20.20s] WARNING: scan failed",
                             mnt_points[i]);
                con_write_line_at_linux(con, status_start + i, con_width, content);
            }
            if (n > 0) total += n;
        }
        /* Move cursor past the status block */
        if (has_con) {
            char seq[32];
            snprintf(seq, sizeof(seq), "\x1b[%d;1H\n", status_start + nmounts + 1);
            tty_write_l(con, seq, strlen(seq));
        }
        return total;
    }

    /* ---- spawn one thread per mount (non-WSL parallel mode) ----------- */
    ScanThreadDataL  td[26];
    PlatformHandle   handles[26];
    int              nhandles = 0;

    for (int i = 0; i < nmounts; i++) {
        td[i].db             = db;
        td[i].drive_slot     = slots[i];
        td[i].include_hidden = include_hidden;
        td[i].include_system = include_system;
        td[i].status         = &statuses[i];
        td[i].dirs_found     = 0;

        handles[nhandles] = platform_thread_create(linux_drive_thread, &td[i]);
        if (handles[nhandles]) {
            nhandles++;
        } else {
            /* Thread creation failed -- run inline */
            linux_drive_thread(&td[i]);
        }
    }

    /* ---- 100 ms display loop ----------------------------------------- */
    unsigned long deadline = platform_tick_ms() + SCAN_TIMEOUT_MS;

    for (;;) {
        bool all_done = true;
        for (int i = 0; i < nmounts; i++)
            if (!statuses[i].is_done) { all_done = false; break; }

        bool timed_out = !all_done && (platform_tick_ms() >= deadline);

        if (has_con) {
            for (int i = 0; i < nmounts; i++) {
                DriveStatus *s = &statuses[i];
                char content[256];
                if (s->is_done) {
                    snprintf(content, sizeof(content),
                             "  [%-20s] COMPLETE (%d directories)",
                             mnt_points[i], s->final_count);
                } else if (timed_out) {
                    snprintf(content, sizeof(content),
                             "  [%-20s] WARNING: scan timed out",
                             mnt_points[i]);
                } else {
                    snprintf(content, sizeof(content),
                             "  [%-20s] %5ld scanned  %s",
                             mnt_points[i],
                             (long)s->dir_count,
                             s->current_path[0] ? s->current_path : "(starting...)");
                }
                con_write_line_at_linux(con, status_start + i, con_width, content);
            }
        }

        if (all_done || timed_out) break;
        platform_sleep_ms(100);
    }

    /* Move cursor past the status block */
    if (has_con) {
        char seq[32];
        snprintf(seq, sizeof(seq), "\x1b[%d;1H\n", status_start + nmounts + 1);
        tty_write_l(con, seq, strlen(seq));
    }

    /* ---- wait for threads to finish ----------------------------------- */
    for (int j = 0; j < nhandles; j++)
        platform_thread_wait(handles[j], 0xFFFFFFFFUL);

    /* ---- total -------------------------------------------------------- */
    int total = 0;
    for (int i = 0; i < nmounts; i++)
        total += td[i].dirs_found;

    return total;
}

#else
#error Unsupported platform in scanner.c
#endif
