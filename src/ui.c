/*
 * ui.c  --  Interactive scrollable selection list for NCD
 *
 * Platform implementations:
 *   Windows -- Win32 console handles (CONOUT$/CONIN$, ANSI mode)
 *   Linux   -- POSIX /dev/tty + termios raw mode + ANSI escape sequences
 *
 * Common UI logic (draw_box, navigate_run, etc.) is shared between platforms.
 * Platform-specific code is isolated to the I/O helper functions.
 */

#include "ui.h"
#include "platform.h"
#include "database.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if NCD_PLATFORM_WINDOWS
#define strncasecmp _strnicmp
#endif

/* Define PLATFORM_VK_DELETE if not available in platform headers */
#ifndef PLATFORM_VK_DELETE
#define PLATFORM_VK_DELETE 0x2E
#endif

/* ================================================================ helpers */

/* ANSI colour sequences */
#define ANSI_RESET   "\x1b[0m"
#define ANSI_REVERSE "\x1b[7m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_CYAN    "\x1b[36m"

/* Key codes returned by read_key() */
#define KEY_UP        1
#define KEY_DOWN      2
#define KEY_PGUP      3
#define KEY_PGDN      4
#define KEY_HOME      5
#define KEY_END       6
#define KEY_ENTER     7
#define KEY_ESC       8
#define KEY_LEFT      9
#define KEY_RIGHT     10
#define KEY_TAB       11
#define KEY_BACKSPACE 12
#define KEY_DELETE    13
#define KEY_UNKNOWN   0

#define NAV_RESULT_CANCEL 0
#define NAV_RESULT_SELECT 1
#define NAV_RESULT_BACK   2

/* ---- shared types ------------------------------------------------------- */

typedef struct {
    char  **items;
    int     count;
    int     cap;
} NameList;

typedef struct {
    int          top_row;
    int          box_width;
    int          visible_rows;
    int          count;           /* original total count */
    int          selected;        /* index into filtered[] */
    int          scroll_top;
    const NcdMatch *matches;      /* original unfiltered array */
    const char  *search_str;
    /* Dirty tracking for incremental redraw */
    int          last_selected;
    int          last_scroll_top;
    bool         first_draw;
    /* Live filter */
    char         filter[64];      /* current filter text */
    int          filter_len;      /* length of filter text */
    int         *filtered;        /* index mapping: filtered[i] -> matches[j] */
    int          filtered_count;  /* number of entries passing filter */
} UiState;

typedef struct {
    int      top_row;
    int      box_width;
    int      visible_rows;
    int      sibling_scroll;
    int      child_scroll;
    int      child_sel;
    char     current_path[MAX_PATH];
    char     parent_path[MAX_PATH];
    NameList siblings;
    NameList children;
    int      sibling_sel;
    bool     has_parent;
} NavState;

/* ---- NameList helpers (forward-declared for list_subdirs) -------------- */

static void names_free(NameList *list);
static bool names_push(NameList *list, const char *name);
static void names_sort(NameList *list);

/* ======================================================================== */
/* ======================== Platform-specific I/O layer ==================== */
/* ======================================================================== */

/* Each platform must provide:
 *   ui_open_console()        -- open terminal handles
 *   ui_close_console()       -- close/restore
 *   con_write(s)             -- write string to output
 *   con_writeln(s)           -- write string + newline
 *   con_write_padded(s, w)   -- write exactly w chars (pad/truncate)
 *   con_cursor_pos(r, c)     -- absolute cursor move
 *   con_hide_cursor()
 *   con_show_cursor()
 *   clear_area(top, rows, w) -- blank a rectangular region
 *   get_console_size(c,r,cr) -- terminal dimensions + cursor row
 *   read_key()               -- blocking key read, returns KEY_* code
 *   list_subdirs(path, out)  -- fill NameList with subdirectory names
 */

/* Forward declaration for key queue (used by con_read_key in all builds) */
static int key_queue_pop(void);

/* ======================================================================== */
#if NCD_PLATFORM_WINDOWS
/* ======================================================================== */

/* Box-drawing characters in CP437 encoding */
#define BOX_TL  "\xC9"
#define BOX_TR  "\xBB"
#define BOX_BL  "\xC8"
#define BOX_BR  "\xBC"
#define BOX_H   "\xCD"
#define BOX_V   "\xBA"
#define BOX_ML  "\xCC"
#define BOX_MR  "\xB9"
#define SEL_IND ">"

static HANDLE g_hout = INVALID_HANDLE_VALUE;
static HANDLE g_hin  = INVALID_HANDLE_VALUE;
static bool   g_ansi = false;

/* Console attribute handling for Windows - used when ANSI is not available */
static WORD g_orig_console_attr = 0;
static bool g_console_attr_saved = false;

static void ui_open_console(void)
{
    /* Reset saved attributes when opening a new console session */
    g_console_attr_saved = false;
    g_orig_console_attr = 0;
    
    g_hout = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, 0, NULL);
    g_hin  = CreateFileA("CONIN$",  GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, 0, NULL);
    if (g_hout != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(g_hout, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(g_hout, mode)) g_ansi = true;
        }
    }
}

static void ui_close_console(void)
{
    if (g_hout != INVALID_HANDLE_VALUE) CloseHandle(g_hout);
    if (g_hin  != INVALID_HANDLE_VALUE) CloseHandle(g_hin);
    g_hout = g_hin = INVALID_HANDLE_VALUE;
}

static void con_write(const char *s)
{
    if (g_hout == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    WriteConsoleA(g_hout, s, (DWORD)strlen(s), &wr, NULL);
}

static void con_write_padded(const char *s, int width)
{
    int slen = (int)strlen(s);
    char buf[512];
    if (slen >= width) {
        int copy = width - 3; if (copy < 0) copy = 0;
        memcpy(buf, s, (size_t)copy);
        if (width >= 3) { buf[copy]='.'; buf[copy+1]='.'; buf[copy+2]='.'; }
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    } else {
        memcpy(buf, s, (size_t)slen);
        memset(buf + slen, ' ', (size_t)(width - slen));
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    }
    DWORD wr;
    WriteConsoleA(g_hout, buf, (DWORD)strlen(buf), &wr, NULL);
}

static void con_cursor_pos(int row, int col)
{
    COORD c = { (SHORT)col, (SHORT)row };
    SetConsoleCursorPosition(g_hout, c);
}

static void con_hide_cursor(void)
{
    CONSOLE_CURSOR_INFO ci = { 1, FALSE };
    SetConsoleCursorInfo(g_hout, &ci);
}

static void con_show_cursor(void)
{
    CONSOLE_CURSOR_INFO ci = { 10, TRUE };
    SetConsoleCursorInfo(g_hout, &ci);
}

static void con_save_attr(void)
{
    if (g_hout == INVALID_HANDLE_VALUE) return;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(g_hout, &info)) {
        g_orig_console_attr = info.wAttributes;
        g_console_attr_saved = true;
    }
}

static void con_set_highlight(bool on)
{
    if (g_hout == INVALID_HANDLE_VALUE) return;
    
    /* Save original attributes on first call */
    if (!g_console_attr_saved) {
        con_save_attr();
    }
    
    if (on) {
        /* Use ANSI if available, otherwise use console attributes */
        if (g_ansi) {
            con_write(ANSI_REVERSE);
        } else {
            /* Set inverse video using console attributes */
            /* Swap foreground and background colors */
            WORD attr = g_orig_console_attr;
            WORD fg = attr & 0x0F;
            WORD bg = (attr >> 4) & 0x0F;
            /* If background is black (0), use white (7) for highlight bg */
            if (bg == 0) bg = 7;
            /* Swap them */
            SetConsoleTextAttribute(g_hout, (WORD)((fg << 4) | bg));
        }
    } else {
        if (g_ansi) {
            con_write(ANSI_RESET);
        } else {
            /* Restore original attributes */
            if (g_console_attr_saved) {
                SetConsoleTextAttribute(g_hout, g_orig_console_attr);
            }
        }
    }
}

static void clear_area(int top_row, int rows, int width)
{
    for (int r = 0; r < rows; r++) {
        con_cursor_pos(top_row + r, 0);
        char blank[256];
        int w = width < (int)sizeof(blank) - 1 ? width : (int)sizeof(blank) - 1;
        memset(blank, ' ', (size_t)w); blank[w] = '\0';
        DWORD wr;
        WriteConsoleA(g_hout, blank, (DWORD)w, &wr, NULL);
    }
}

static void get_console_size(int *cols, int *rows, int *cur_row)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hout, &csbi)) {
        *cols    = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        *rows    = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
        *cur_row = csbi.dwCursorPosition.Y;
    } else { *cols = 80; *rows = 25; *cur_row = 0; }
}

static int con_read_key(void)
{
    /* Check injected key queue first (for testing/automation) */
    int injected = key_queue_pop();
    if (injected != KEY_UNKNOWN) return injected;

    INPUT_RECORD ir;
    DWORD        nread;
    for (;;) {
        if (!ReadConsoleInputA(g_hin, &ir, 1, &nread)) return KEY_ESC;
        if (ir.EventType != KEY_EVENT)       continue;
        if (!ir.Event.KeyEvent.bKeyDown)     continue;
        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        char ch = ir.Event.KeyEvent.uChar.AsciiChar;
        switch (vk) {
            case VK_UP:     return KEY_UP;
            case VK_DOWN:   return KEY_DOWN;
            case VK_PRIOR:  return KEY_PGUP;
            case VK_NEXT:   return KEY_PGDN;
            case VK_HOME:   return KEY_HOME;
            case VK_END:    return KEY_END;
            case VK_LEFT:   return KEY_LEFT;
            case VK_RIGHT:  return KEY_RIGHT;
            case VK_TAB:    return KEY_TAB;
            case VK_BACK:   return KEY_BACKSPACE;
            case VK_RETURN: return KEY_ENTER;
            case VK_ESCAPE: return KEY_ESC;
            case VK_DELETE: return KEY_DELETE;
            case ' ':       return ' ';
            default:
                /* Return printable ASCII characters directly */
                if (ch >= 32 && ch <= 126) return (int)ch;
                /* Also handle virtual key codes for digits */
                if (vk >= '0' && vk <= '9') return vk;
                continue;
        }
    }
}

static bool con_list_subdirs(const char *dir_path, NameList *out)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    out->items = NULL; out->count = 0; out->cap = 0;
    if (snprintf(pattern, sizeof(pattern), "%s%s*", dir_path, NCD_PATH_SEP) >= (int)sizeof(pattern))
        return false;
    HANDLE h = FindFirstFileExA(pattern, FindExInfoBasic, &fd,
                                FindExSearchLimitToDirectories, NULL,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return true;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (!names_push(out, fd.cFileName)) { FindClose(h); names_free(out); return false; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    names_sort(out);
    return true;
}

/* ======================================================================== */
#elif NCD_PLATFORM_LINUX
/* ======================================================================== */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>

/* Box-drawing characters: plain ASCII (always correct on any terminal) */
#define BOX_TL  "+"
#define BOX_TR  "+"
#define BOX_BL  "+"
#define BOX_BR  "+"
#define BOX_H   "-"
#define BOX_V   "|"
#define BOX_ML  "+"
#define BOX_MR  "+"
#define SEL_IND ">"

static int  g_tty_out = -1;
static int  g_tty_in  = -1;
static bool g_ansi    = true;   /* assume ANSI on Linux terminals */

/* We use the platform_console_open_in which sets raw mode for us. */
static void ui_open_console(void)
{
    PlatformHandle hout = platform_console_open_out(false);
    PlatformHandle hin  = platform_console_open_in();
    g_tty_out = hout ? (int)(intptr_t)hout - 1 : -1;
    g_tty_in  = hin  ? (int)(intptr_t)hin  - 1 : -1;
}

static void ui_close_console(void)
{
    if (g_tty_in  >= 0) platform_handle_close((void *)(intptr_t)(g_tty_in  + 1));
    if (g_tty_out >= 0) platform_handle_close((void *)(intptr_t)(g_tty_out + 1));
    g_tty_in = g_tty_out = -1;
}

/* write() wrapper that discards the return value without -Wunused-result */
static void tty_write_u(int fd, const void *buf, size_t n)
{
    ssize_t r = write(fd, buf, n); (void)r;
}

static void con_write(const char *s)
{
    if (g_tty_out < 0 || !s) return;
    tty_write_u(g_tty_out, s, strlen(s));
}

static void con_write_padded(const char *s, int width)
{
    if (g_tty_out < 0 || width <= 0) return;
    int slen = (int)strlen(s);
    char buf[512];
    if (slen >= width) {
        int copy = width - 3; if (copy < 0) copy = 0;
        memcpy(buf, s, (size_t)copy);
        if (width >= 3) { buf[copy]='.'; buf[copy+1]='.'; buf[copy+2]='.'; }
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    } else {
        memcpy(buf, s, (size_t)slen);
        memset(buf + slen, ' ', (size_t)(width - slen));
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    }
    size_t blen = strlen(buf);
    tty_write_u(g_tty_out, buf, blen);
}

static void con_cursor_pos(int row, int col)
{
    if (g_tty_out < 0) return;
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[%d;%dH", row + 1, col + 1);
    tty_write_u(g_tty_out, seq, (size_t)n);
}

static void con_hide_cursor(void)
{
    if (g_tty_out >= 0) tty_write_u(g_tty_out, "\x1b[?25l", 6);
}

static void con_show_cursor(void)
{
    if (g_tty_out >= 0) tty_write_u(g_tty_out, "\x1b[?25h", 6);
}

/* Linux uses ANSI escape sequences for highlighting */
static void con_set_highlight(bool on)
{
    if (g_tty_out < 0) return;
    if (on) {
        tty_write_u(g_tty_out, ANSI_REVERSE, strlen(ANSI_REVERSE));
    } else {
        tty_write_u(g_tty_out, ANSI_RESET, strlen(ANSI_RESET));
    }
}

static void clear_area(int top_row, int rows, int width)
{
    if (g_tty_out < 0) return;
    char blank[256];
    int w = width < (int)sizeof(blank) - 1 ? width : (int)sizeof(blank) - 1;
    memset(blank, ' ', (size_t)w); blank[w] = '\0';
    for (int r = 0; r < rows; r++) {
        char seq[32];
        int n = snprintf(seq, sizeof(seq), "\x1b[%d;1H\x1b[2K", top_row + r + 1);
        tty_write_u(g_tty_out, seq, (size_t)n);
        tty_write_u(g_tty_out, blank, (size_t)w);
    }
}

static void get_console_size(int *cols, int *rows, int *cur_row)
{
    *cols = 80; *rows = 25; *cur_row = 0;
    if (g_tty_out < 0) return;
    struct winsize ws;
    if (ioctl(g_tty_out, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row > 0 ? ws.ws_row : 25;
    }
    /* Query cursor row via ANSI CPR */
    if (write(g_tty_out, "\x1b[6n", 4) == 4) {
        char resp[32] = {0};
        int i = 0;
        fd_set fds; struct timeval tv = {0, 300000};
        FD_ZERO(&fds); FD_SET(g_tty_in >= 0 ? g_tty_in : g_tty_out, &fds);
        int rfd = g_tty_in >= 0 ? g_tty_in : g_tty_out;
        while (i < (int)sizeof(resp) - 1 &&
               select(rfd + 1, &fds, NULL, NULL, &tv) > 0) {
            if (read(rfd, &resp[i], 1) <= 0) break;
            if (resp[i++] == 'R') break;
            FD_ZERO(&fds); FD_SET(rfd, &fds); tv.tv_usec = 50000;
        }
        int row = 0, col2 = 0;
        if (sscanf(resp, "\x1b[%d;%dR", &row, &col2) >= 1 ||
            sscanf(resp, "[%d;%dR",     &row, &col2) >= 1)
            *cur_row = row > 0 ? row - 1 : 0;
    }
}

static int con_read_key(void)
{
    /* Check injected key queue first (for testing/automation) */
    int injected = key_queue_pop();
    if (injected != KEY_UNKNOWN) return injected;

    if (g_tty_in < 0) return KEY_ESC;
    PlatformHandle h = (PlatformHandle)(intptr_t)(g_tty_in + 1);
    unsigned short vk = 0;
    if (!platform_console_read_key_vk(h, &vk)) return KEY_ESC;
    switch (vk) {
        case PLATFORM_VK_UP:    return KEY_UP;
        case PLATFORM_VK_DOWN:  return KEY_DOWN;
        case PLATFORM_VK_PRIOR: return KEY_PGUP;
        case PLATFORM_VK_NEXT:  return KEY_PGDN;
        case PLATFORM_VK_HOME:  return KEY_HOME;
        case PLATFORM_VK_END:   return KEY_END;
        case PLATFORM_VK_LEFT:  return KEY_LEFT;
        case PLATFORM_VK_RIGHT: return KEY_RIGHT;
        case PLATFORM_VK_TAB:   return KEY_TAB;
        case PLATFORM_VK_BACK:  return KEY_BACKSPACE;
        case PLATFORM_VK_RETURN:return KEY_ENTER;
        case PLATFORM_VK_ESCAPE:return KEY_ESC;
        case PLATFORM_VK_DELETE:return KEY_DELETE;
        case ' ':               return ' ';
        default:
            /* Return printable ASCII characters directly */
            if (vk >= 32 && vk <= 126) return (int)vk;
            /* Also handle digits */
            if (vk >= '0' && vk <= '9') return vk;
            return KEY_UNKNOWN;
    }
}

static bool con_list_subdirs(const char *dir_path, NameList *out)
{
    out->items = NULL; out->count = 0; out->cap = 0;
    DIR *d = opendir(dir_path);
    if (!d) return true;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        /* Determine if directory */
        bool is_dir = false;
        if (ent->d_type == DT_DIR) {
            is_dir = true;
        } else if (ent->d_type == DT_UNKNOWN) {
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
            struct stat st;
            if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = true;
        }
        if (!is_dir) continue;
        if (!names_push(out, ent->d_name)) { closedir(d); names_free(out); return false; }
    }
    closedir(d);
    names_sort(out);
    return true;
}

/* ======================================================================== */
#else
#error Unsupported platform in ui.c
#endif
/* ======================================================================== */

/* ======================================================================== */
/* =================== UiIoOps abstraction layer ========================== */
/* ======================================================================== */

static UiIoOps g_default_ops;
static UiIoOps *g_ui_ops = NULL;

#ifndef NDEBUG
/* Forward declaration for stdio TUI test backend.
 * Returns pointer to static ops struct, or NULL if NCD_TEST_MODE is not set. */
static UiIoOps *stdio_backend_init(void);
#endif

#ifdef NCD_TEST_BUILD
/* Forward declarations for test backend */
static void test_backend_open_console(void);
static void test_backend_close_console(void);
static void test_backend_write(const char *s);
static void test_backend_write_at(int row, int col, const char *s);
static void test_backend_write_padded(const char *s, int width);
static void test_backend_cursor_pos(int row, int col);
static void test_backend_hide_cursor(void);
static void test_backend_show_cursor(void);
static void test_backend_clear_area(int top_row, int rows, int width);
static void test_backend_get_size(int *cols, int *rows, int *cur_row);
static int  test_backend_read_key(void);
#endif /* NCD_TEST_BUILD */

/* ======================================================================== */
/* =================== Scripted key queue (all builds) ==================== */
/* ======================================================================== */

#define KEY_QUEUE_MAX 256

static int  g_key_queue[KEY_QUEUE_MAX];
static int  g_key_queue_head = 0;
static int  g_key_queue_tail = 0;

static void load_keys_from_env(void)
{
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    
    const char *keys = getenv("NCD_UI_KEYS");
    if (keys && keys[0]) {
        if (keys[0] == '@') {
            ui_inject_keys_from_file(keys + 1);
        } else {
            ui_inject_keys(keys);
        }
    }
}

static void init_default_ops(void)
{
    g_default_ops.open_console  = ui_open_console;
    g_default_ops.close_console = ui_close_console;
    g_default_ops.write         = con_write;
    g_default_ops.write_at      = NULL; /* not used directly */
    g_default_ops.write_padded  = con_write_padded;
    g_default_ops.cursor_pos    = con_cursor_pos;
    g_default_ops.hide_cursor   = con_hide_cursor;
    g_default_ops.show_cursor   = con_show_cursor;
    g_default_ops.clear_area    = clear_area;
    g_default_ops.get_size      = get_console_size;
    g_default_ops.read_key      = con_read_key;
    g_default_ops.ansi_enabled  = g_ansi;
}

void ui_set_io_backend(UiIoOps *ops)
{
    g_ui_ops = ops;
}

UiIoOps *ui_get_io_backend(void)
{
    if (!g_ui_ops) {
        /* Auto-load keys from environment (enables testing and automation) */
        load_keys_from_env();

#ifndef NDEBUG
        /* In debug builds, check for stdio TUI test mode */
        {
            UiIoOps *stdio_ops = stdio_backend_init();
            if (stdio_ops) {
                g_ui_ops = stdio_ops;
                return g_ui_ops;
            }
        }
#endif
        init_default_ops();
        g_ui_ops = &g_default_ops;
    }
    return g_ui_ops;
}

#ifdef NCD_TEST_BUILD
/* ======================================================================== */
/* =================== Test backend (in-memory grid) ====================== */
/* ======================================================================== */

#define TEST_GRID_MAX_COLS 160
#define TEST_GRID_MAX_ROWS 60

typedef struct {
    char grid[TEST_GRID_MAX_ROWS][TEST_GRID_MAX_COLS];
    int  cols;
    int  rows;
    int  cur_row;
    int  cur_col;
    bool cursor_hidden;
} TestBackendState;

static TestBackendState g_test_backend;
static bool g_test_backend_active = false;

static void test_backend_open_console(void)
{
    /* no-op */
}

static void test_backend_close_console(void)
{
    /* no-op */
}

static void test_backend_write(const char *s)
{
    if (!g_test_backend_active || !s) return;
    while (*s) {
        if (g_test_backend.cur_row >= 0 && g_test_backend.cur_row < g_test_backend.rows &&
            g_test_backend.cur_col >= 0 && g_test_backend.cur_col < g_test_backend.cols) {
            g_test_backend.grid[g_test_backend.cur_row][g_test_backend.cur_col] = *s;
        }
        g_test_backend.cur_col++;
        s++;
    }
}

static void test_backend_write_at(int row, int col, const char *s)
{
    if (!g_test_backend_active) return;
    g_test_backend.cur_row = row;
    g_test_backend.cur_col = col;
    test_backend_write(s);
}

static void test_backend_write_padded(const char *s, int width)
{
    if (!g_test_backend_active || width <= 0) return;
    int slen = (int)strlen(s);
    char buf[512];
    if (slen >= width) {
        int copy = width - 3; if (copy < 0) copy = 0;
        memcpy(buf, s, (size_t)copy);
        if (width >= 3) { buf[copy]='.'; buf[copy+1]='.'; buf[copy+2]='.'; }
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    } else {
        memcpy(buf, s, (size_t)slen);
        memset(buf + slen, ' ', (size_t)(width - slen));
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    }
    test_backend_write(buf);
}

static void test_backend_cursor_pos(int row, int col)
{
    if (!g_test_backend_active) return;
    g_test_backend.cur_row = row;
    g_test_backend.cur_col = col;
}

static void test_backend_hide_cursor(void)
{
    if (!g_test_backend_active) return;
    g_test_backend.cursor_hidden = true;
}

static void test_backend_show_cursor(void)
{
    if (!g_test_backend_active) return;
    g_test_backend.cursor_hidden = false;
}

static void test_backend_clear_area(int top_row, int num_rows, int width)
{
    if (!g_test_backend_active) return;
    for (int r = top_row; r < top_row + num_rows && r < g_test_backend.rows; r++) {
        if (r < 0) continue;
        int w = width < g_test_backend.cols ? width : g_test_backend.cols;
        memset(g_test_backend.grid[r], ' ', (size_t)w);
        if (w < g_test_backend.cols) g_test_backend.grid[r][w] = '\0';
        else g_test_backend.grid[r][g_test_backend.cols - 1] = '\0';
    }
}

static void test_backend_get_size(int *cols, int *rows, int *cur_row)
{
    if (!g_test_backend_active) {
        *cols = 80; *rows = 25; *cur_row = 0;
        return;
    }
    *cols = g_test_backend.cols;
    *rows = g_test_backend.rows;
    *cur_row = g_test_backend.cur_row;
}

UiIoOps *ui_create_test_backend(int cols, int rows)
{
    UiIoOps *ops = (UiIoOps *)calloc(1, sizeof(UiIoOps));
    if (!ops) return NULL;
    ops->open_console  = test_backend_open_console;
    ops->close_console = test_backend_close_console;
    ops->write         = test_backend_write;
    ops->write_at      = test_backend_write_at;
    ops->write_padded  = test_backend_write_padded;
    ops->cursor_pos    = test_backend_cursor_pos;
    ops->hide_cursor   = test_backend_hide_cursor;
    ops->show_cursor   = test_backend_show_cursor;
    ops->clear_area    = test_backend_clear_area;
    ops->get_size      = test_backend_get_size;
    ops->read_key      = test_backend_read_key;
    ops->ansi_enabled  = true;

    memset(&g_test_backend, 0, sizeof(g_test_backend));
    if (cols > TEST_GRID_MAX_COLS) cols = TEST_GRID_MAX_COLS;
    if (rows > TEST_GRID_MAX_ROWS) rows = TEST_GRID_MAX_ROWS;
    if (cols < 1) cols = 80;
    if (rows < 1) rows = 25;
    g_test_backend.cols = cols;
    g_test_backend.rows = rows;
    g_test_backend.cur_row = 0;
    g_test_backend.cur_col = 0;
    g_test_backend.cursor_hidden = false;
    for (int r = 0; r < rows; r++) {
        memset(g_test_backend.grid[r], ' ', (size_t)cols);
        g_test_backend.grid[r][cols] = '\0';
    }
    g_test_backend_active = true;
    return ops;
}

void ui_destroy_test_backend(UiIoOps *ops)
{
    if (ops) free(ops);
    g_test_backend_active = false;
}

const char *ui_test_backend_row(int row)
{
    if (!g_test_backend_active || row < 0 || row >= g_test_backend.rows)
        return "";
    /* trim trailing spaces for readability */
    char *line = g_test_backend.grid[row];
    int len = (int)strlen(line);
    while (len > 0 && line[len - 1] == ' ') len--;
    line[len] = '\0';
    return line;
}

int ui_test_backend_find_row(const char *substring)
{
    if (!g_test_backend_active || !substring) return -1;
    for (int r = 0; r < g_test_backend.rows; r++) {
        if (strstr(ui_test_backend_row(r), substring) != NULL)
            return r;
    }
    return -1;
}
#endif /* NCD_TEST_BUILD -- test backend grid */

/* Key queue functions - available in all builds for automation/testing */
static int key_queue_pop(void)
{
    if (g_key_queue_head == g_key_queue_tail) return KEY_UNKNOWN;
    int k = g_key_queue[g_key_queue_head];
    g_key_queue_head = (g_key_queue_head + 1) % KEY_QUEUE_MAX;
    return k;
}

static bool key_queue_push(int k)
{
    int next = (g_key_queue_tail + 1) % KEY_QUEUE_MAX;
    if (next == g_key_queue_head) return false; /* full */
    g_key_queue[g_key_queue_tail] = k;
    g_key_queue_tail = next;
    return true;
}

static int parse_key_token(const char *tok)
{
    while (*tok == ' ' || *tok == '\t') tok++;
    size_t len = strlen(tok);
    while (len > 0 && (tok[len - 1] == ' ' || tok[len - 1] == '\t' || tok[len - 1] == '\n' || tok[len - 1] == '\r')) len--;

    if (len == 0) return KEY_UNKNOWN;

    if (len >= 5 && strncasecmp(tok, "TEXT:", 5) == 0) {
        /* TEXT tokens are expanded one character at a time.
         * We only return the first char here; caller must handle multi-char. */
        /* Actually, we handle this at the parser level. */
        return KEY_UNKNOWN;
    }

    if (strncasecmp(tok, "UP", len) == 0) return KEY_UP;
    if (strncasecmp(tok, "DOWN", len) == 0) return KEY_DOWN;
    if (strncasecmp(tok, "LEFT", len) == 0) return KEY_LEFT;
    if (strncasecmp(tok, "RIGHT", len) == 0) return KEY_RIGHT;
    if (strncasecmp(tok, "PGUP", len) == 0) return KEY_PGUP;
    if (strncasecmp(tok, "PGDN", len) == 0) return KEY_PGDN;
    if (strncasecmp(tok, "HOME", len) == 0) return KEY_HOME;
    if (strncasecmp(tok, "END", len) == 0) return KEY_END;
    if (strncasecmp(tok, "ENTER", len) == 0) return KEY_ENTER;
    if (strncasecmp(tok, "ESC", len) == 0) return KEY_ESC;
    if (strncasecmp(tok, "TAB", len) == 0) return KEY_TAB;
    if (strncasecmp(tok, "BACKSPACE", len) == 0) return KEY_BACKSPACE;
    if (strncasecmp(tok, "DELETE", len) == 0) return KEY_DELETE;
    if (strncasecmp(tok, "SPACE", len) == 0) return ' ';

    /* Single printable char */
    if (len == 1 && (unsigned char)tok[0] >= 32 && (unsigned char)tok[0] <= 126)
        return (unsigned char)tok[0];

    return KEY_UNKNOWN;
}

int ui_inject_keys(const char *key_script)
{
    if (!key_script || !key_script[0]) return 0;

    char *buf = strdup(key_script);
    if (!buf) return -1;

    char *p = buf;
    while (*p) {
        char *comma = strchr(p, ',');
        if (comma) *comma = '\0';

        /* trim whitespace */
        char *tok = p;
        while (*tok == ' ' || *tok == '\t') tok++;
        size_t len = strlen(tok);
        while (len > 0 && (tok[len - 1] == ' ' || tok[len - 1] == '\t' || tok[len - 1] == '\n' || tok[len - 1] == '\r')) len--;

        if (len >= 5 && strncasecmp(tok, "TEXT:", 5) == 0) {
            const char *text = tok + 5;
            while (*text) {
                if (!key_queue_push((unsigned char)*text)) { free(buf); return -1; }
                text++;
            }
        } else {
            int k = parse_key_token(tok);
            if (k != KEY_UNKNOWN) {
                if (!key_queue_push(k)) { free(buf); return -1; }
            }
        }

        if (!comma) break;
        p = comma + 1;
    }

    free(buf);
    return 0;
}

int ui_inject_keys_from_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    buf[sz] = '\0';
    fclose(f);
    int rc = ui_inject_keys(buf);
    free(buf);
    return rc;
}

void ui_clear_injected_keys(void)
{
    g_key_queue_head = g_key_queue_tail = 0;
}

bool ui_injected_keys_empty(void)
{
    return g_key_queue_head == g_key_queue_tail;
}

#ifdef NCD_TEST_BUILD
static int test_backend_read_key(void)
{
    /* Check injected key queue first */
    if (g_key_queue_head != g_key_queue_tail) {
        return key_queue_pop();
    }

    /* Optional timeout from environment */
    const char *to_env = getenv("NCD_UI_KEY_TIMEOUT_MS");
    if (to_env) {
        int to_ms = atoi(to_env);
        if (to_ms > 0) {
            int waited = 0;
            while (waited < to_ms) {
                if (g_key_queue_head != g_key_queue_tail)
                    return key_queue_pop();
#if NCD_PLATFORM_WINDOWS
                Sleep(10);
#else
                usleep(10000);
#endif
                waited += 10;
            }
            return KEY_ESC;
        }
    }

    return KEY_ESC;
}

/* ======================================================================== */
/* =================== Snapshot API ======================================= */
/* ======================================================================== */

UiSnapshot *ui_snapshot_capture(void)
{
    if (!g_test_backend_active) return NULL;

    UiSnapshot *snap = (UiSnapshot *)calloc(1, sizeof(UiSnapshot));
    if (!snap) return NULL;

    snap->rows = (char **)malloc(sizeof(char *) * g_test_backend.rows);
    if (!snap->rows) { free(snap); return NULL; }

    snap->row_count = g_test_backend.rows;
    snap->width = g_test_backend.cols;
    snap->selected_row = -1;
    snap->scroll_top = 0;
    snap->filter[0] = '\0';

    for (int r = 0; r < g_test_backend.rows; r++) {
        const char *row = ui_test_backend_row(r);
        snap->rows[r] = strdup(row);
        if (strstr(row, SEL_IND) != NULL && strstr(row, BOX_V) != NULL) {
            snap->selected_row = r;
        }
        if (strstr(row, "NCD") != NULL && (strstr(row, "match") != NULL || strstr(row, "filter") != NULL)) {
            snap->has_header = true;
            const char *f = strstr(row, "filter:");
            if (f) {
                f += 7;
                while (*f == ' ' || *f == '\"') f++;
                char *dst = snap->filter;
                int n = 0;
                while (*f && *f != '\"' && n < 63) { dst[n++] = *f++; }
                dst[n] = '\0';
            }
        }
    }
    return snap;
}

void ui_snapshot_free(UiSnapshot *snap)
{
    if (!snap) return;
    if (snap->rows) {
        for (int i = 0; i < snap->row_count; i++) free(snap->rows[i]);
        free(snap->rows);
    }
    free(snap);
}

#endif /* NCD_TEST_BUILD -- test backend read_key + snapshot */

/* ======================================================================== */
/* =================== Stdio TUI test backend (debug builds only) ========= */
/* ======================================================================== */

/*
 * When NCD_TEST_MODE is set (debug builds only), the TUI renders to an
 * in-memory grid and dumps the final frame to stdout on close.  Keys are
 * read from the injected key queue (set via NCD_UI_KEYS, which also supports
 * @filename syntax to load keys from a file).
 * This lets batch files drive the TUI and capture output for comparison
 * against expected output files.
 *
 * NCD_TEST_MODE may optionally specify terminal dimensions as "cols,rows"
 * (e.g., NCD_TEST_MODE=80,25).  If no comma is present, defaults are used.
 */
#ifndef NDEBUG

#define STDIO_GRID_MAX_COLS 160
#define STDIO_GRID_MAX_ROWS 60

static char  g_stdio_grid[STDIO_GRID_MAX_ROWS][STDIO_GRID_MAX_COLS];
static int   g_stdio_cols = 80;
static int   g_stdio_rows = 25;
static int   g_stdio_cur_row = 0;
static int   g_stdio_cur_col = 0;
static bool  g_stdio_active = false;

static void stdio_open_console(void)
{
    const char *test_mode = getenv("NCD_TEST_MODE");
    if (test_mode && test_mode[0]) {
        const char *comma = strchr(test_mode, ',');
        if (comma) {
            int c = atoi(test_mode);
            int r = atoi(comma + 1);
            if (c > 0 && c <= STDIO_GRID_MAX_COLS) g_stdio_cols = c;
            if (r > 0 && r <= STDIO_GRID_MAX_ROWS) g_stdio_rows = r;
        }
    }
    for (int r = 0; r < g_stdio_rows; r++) {
        memset(g_stdio_grid[r], ' ', (size_t)g_stdio_cols);
        g_stdio_grid[r][g_stdio_cols] = '\0';
    }
    g_stdio_cur_row = 0;
    g_stdio_cur_col = 0;
    g_stdio_active = true;
}

static void stdio_close_console(void)
{
    if (!g_stdio_active) return;
    /* Dump final frame to stdout */
    for (int r = 0; r < g_stdio_rows; r++) {
        /* Trim trailing spaces */
        char *line = g_stdio_grid[r];
        int len = (int)strlen(line);
        while (len > 0 && line[len - 1] == ' ') len--;
        line[len] = '\0';
        printf("%s\n", line);
    }
    fflush(stdout);
    g_stdio_active = false;
}

static void stdio_write(const char *s)
{
    if (!g_stdio_active || !s) return;
    while (*s) {
        if (*s == '\n') {
            g_stdio_cur_row++;
            g_stdio_cur_col = 0;
        } else if (*s == '\r') {
            g_stdio_cur_col = 0;
        } else {
            if (g_stdio_cur_row >= 0 && g_stdio_cur_row < g_stdio_rows &&
                g_stdio_cur_col >= 0 && g_stdio_cur_col < g_stdio_cols) {
                g_stdio_grid[g_stdio_cur_row][g_stdio_cur_col] = *s;
            }
            g_stdio_cur_col++;
        }
        s++;
    }
}

static void stdio_write_at(int row, int col, const char *s)
{
    if (!g_stdio_active) return;
    g_stdio_cur_row = row;
    g_stdio_cur_col = col;
    stdio_write(s);
}

static void stdio_write_padded(const char *s, int width)
{
    if (!g_stdio_active || width <= 0) return;
    int slen = (int)strlen(s);
    char buf[512];
    if (slen >= width) {
        int copy = width - 3; if (copy < 0) copy = 0;
        memcpy(buf, s, (size_t)copy);
        if (width >= 3) { buf[copy]='.'; buf[copy+1]='.'; buf[copy+2]='.'; }
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    } else {
        memcpy(buf, s, (size_t)slen);
        memset(buf + slen, ' ', (size_t)(width - slen));
        buf[width < (int)sizeof(buf) ? width : (int)sizeof(buf)-1] = '\0';
    }
    stdio_write(buf);
}

static void stdio_cursor_pos(int row, int col)
{
    g_stdio_cur_row = row;
    g_stdio_cur_col = col;
}

static void stdio_hide_cursor(void) { /* no-op */ }
static void stdio_show_cursor(void) { /* no-op */ }

static void stdio_clear_area(int top_row, int num_rows, int width)
{
    if (!g_stdio_active) return;
    for (int r = top_row; r < top_row + num_rows && r < g_stdio_rows; r++) {
        if (r < 0) continue;
        int w = width < g_stdio_cols ? width : g_stdio_cols;
        memset(g_stdio_grid[r], ' ', (size_t)w);
        if (w < g_stdio_cols) g_stdio_grid[r][w] = '\0';
        else g_stdio_grid[r][g_stdio_cols - 1] = '\0';
    }
}

static void stdio_get_size(int *cols, int *rows, int *cur_row)
{
    *cols = g_stdio_cols;
    *rows = g_stdio_rows;
    *cur_row = g_stdio_cur_row;
}

static int queue_only_read_key(void)
{
    if (g_key_queue_head != g_key_queue_tail) {
        return key_queue_pop();
    }
    return KEY_UNKNOWN;
}

static UiIoOps g_stdio_ops;

static UiIoOps *stdio_backend_init(void)
{
    const char *env = getenv("NCD_TEST_MODE");
    fprintf(stderr, "STDIO_INIT: NCD_TEST_MODE=%s\n", env ? env : "(null)");
    if (!env || !env[0]) return NULL;
    fprintf(stderr, "STDIO_INIT: activating stdio backend\n");

    g_stdio_ops.open_console  = stdio_open_console;
    g_stdio_ops.close_console = stdio_close_console;
    g_stdio_ops.write         = stdio_write;
    g_stdio_ops.write_at      = stdio_write_at;
    g_stdio_ops.write_padded  = stdio_write_padded;
    g_stdio_ops.cursor_pos    = stdio_cursor_pos;
    g_stdio_ops.hide_cursor   = stdio_hide_cursor;
    g_stdio_ops.show_cursor   = stdio_show_cursor;
    g_stdio_ops.clear_area    = stdio_clear_area;
    g_stdio_ops.get_size      = stdio_get_size;
    g_stdio_ops.read_key      = queue_only_read_key;
    g_stdio_ops.ansi_enabled  = false;
    return &g_stdio_ops;
}

#endif /* !NDEBUG -- stdio TUI test backend */

/* ======================================================================== */
/* =================== Shared helpers (use ops from above) ================ */
/* ======================================================================== */

static void names_free(NameList *list)
{
    if (!list) return;
    for (int i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    list->items = NULL; list->count = 0; list->cap = 0;
}

static bool names_push(NameList *list, const char *name)
{
    if (list->count >= list->cap) {
        int nc = list->cap ? list->cap * 2 : 16;
        char **tmp = (char **)realloc(list->items, (size_t)nc * sizeof(char *));
        if (!tmp) return false;
        list->items = tmp; list->cap = nc;
    }
    size_t len = strlen(name);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return false;
    memcpy(copy, name, len + 1);
    list->items[list->count++] = copy;
    return true;
}

static int cmp_names_ci(const void *a, const void *b)
{
    return _stricmp(*(const char * const *)a, *(const char * const *)b);
}

static void names_sort(NameList *list)
{
    if (!list || list->count < 2) return;
    qsort(list->items, (size_t)list->count, sizeof(char *), cmp_names_ci);
}

/* ---- scroll clamping -------------------------------------------------- */

static void clamp_scroll(UiState *st)
{
    if (st->selected < st->scroll_top)
        st->scroll_top = st->selected;
    if (st->selected >= st->scroll_top + st->visible_rows)
        st->scroll_top = st->selected - st->visible_rows + 1;
    if (st->scroll_top < 0) st->scroll_top = 0;
}

/* Apply filter and rebuild filtered index array */
static void apply_filter(UiState *st)
{
    st->filtered_count = 0;
    for (int i = 0; i < st->count; i++) {
        const char *path = st->matches[i].full_path;

        if (st->filter_len == 0) {
            /* No filter -- include everything */
            st->filtered[st->filtered_count++] = i;
            continue;
        }

        /* Filter against the complete text line shown in the UI (full path) */
        if (platform_strcasestr(path, st->filter)) {
            st->filtered[st->filtered_count++] = i;
        }
    }

    /* Reset navigation */
    st->selected = 0;
    st->scroll_top = 0;
    st->first_draw = true;  /* force full redraw */
}

static void nav_clamp_scroll(NavState *st)
{
    if (st->sibling_sel < 0) st->sibling_sel = 0;
    if (st->siblings.count > 0 && st->sibling_sel >= st->siblings.count)
        st->sibling_sel = st->siblings.count - 1;
    if (st->sibling_sel < st->sibling_scroll) st->sibling_scroll = st->sibling_sel;
    if (st->sibling_sel >= st->sibling_scroll + st->visible_rows)
        st->sibling_scroll = st->sibling_sel - st->visible_rows + 1;
    if (st->sibling_scroll < 0) st->sibling_scroll = 0;

    if (st->child_sel < 0) st->child_sel = 0;
    if (st->children.count > 0 && st->child_sel >= st->children.count)
        st->child_sel = st->children.count - 1;
    if (st->child_sel < st->child_scroll) st->child_scroll = st->child_sel;
    if (st->child_sel >= st->child_scroll + st->visible_rows)
        st->child_scroll = st->child_sel - st->visible_rows + 1;
    if (st->child_scroll < 0) st->child_scroll = 0;
    if (st->children.count <= st->visible_rows) st->child_scroll = 0;
}

/* ======================================================================== */
/* ========================= Shared drawing logic ========================== */
/* ======================================================================== */

/* Helper macros to route through g_ui_ops */
#define OP_WRITE(s)        g_ui_ops->write(s)
#define OP_WRITELN(s)      do { g_ui_ops->write(s); g_ui_ops->write("\n"); } while(0)
#define OP_WRITE_PAD(s,w)  g_ui_ops->write_padded(s,w)
#define OP_CURSOR(r,c)     g_ui_ops->cursor_pos(r,c)
#define OP_HIDE()          g_ui_ops->hide_cursor()
#define OP_SHOW()          g_ui_ops->show_cursor()
#define OP_CLEAR(t,r,w)    g_ui_ops->clear_area(t,r,w)
#define OP_SIZE(c,r,cr)    g_ui_ops->get_size(c,r,cr)
#define OP_ANSI            g_ui_ops->ansi_enabled

/* Helper: Draw a single list row */
static void draw_list_row(UiState *st, int row, int vis_idx, bool is_selected)
{
    int w = st->box_width;
    int inner = w - 2;
    char line[512];
    int list_start = st->top_row + 3;

    OP_CURSOR(list_start + row, 0);
    OP_WRITE(BOX_V);
    if (is_selected) con_set_highlight(true);

    if (vis_idx >= 0 && vis_idx < st->filtered_count) {
        int real_idx = st->filtered[vis_idx];
        snprintf(line, sizeof(line), " %s %.*s",
                 is_selected ? SEL_IND : " ",
                 (int)(sizeof(line) - 4), st->matches[real_idx].full_path);
    } else {
        line[0] = '\0';
    }
    OP_WRITE_PAD(line, inner);
    if (is_selected) con_set_highlight(false);
    OP_WRITE(BOX_V);
}

/* Helper: Draw scrollbar */
static void draw_scrollbar(UiState *st)
{
    int w = st->box_width;
    int list_start = st->top_row + 3;

    if (st->filtered_count > st->visible_rows) {
        int track = st->visible_rows;
        int thumb = (int)((double)st->scroll_top / (st->filtered_count - st->visible_rows)
                          * (track - 1));
        for (int r = 0; r < track; r++) {
            OP_CURSOR(list_start + r, w - 1);
            OP_WRITE(r == thumb ? "#" : ".");
        }
    }
}

static void draw_box(UiState *st)
{
    int w = st->box_width;
    int inner = w - 2;
    int i;

    /* Full redraw on first draw */
    if (st->first_draw) {
        st->first_draw = false;
        
        /* Draw box frame and header */
        OP_CURSOR(st->top_row, 0);
        OP_WRITE(BOX_TL);
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
        OP_WRITE(BOX_TR);

        OP_CURSOR(st->top_row + 1, 0);
        OP_WRITE(BOX_V);
        if (OP_ANSI) OP_WRITE(ANSI_BOLD ANSI_CYAN);
        char header[256];
        if (st->filter_len > 0) {
            snprintf(header, sizeof(header),
                     "  NCD  --  %d/%d for \"%s\"  filter: \"%s\"",
                     st->filtered_count, st->count, st->search_str, st->filter);
        } else {
            snprintf(header, sizeof(header),
                     "  NCD  --  %d match%s for \"%s\"  (type to filter)",
                     st->count, st->count == 1 ? "" : "es", st->search_str);
        }
        OP_WRITE_PAD(header, inner);
        if (OP_ANSI) OP_WRITE(ANSI_RESET);
        OP_WRITE(BOX_V);

        OP_CURSOR(st->top_row + 2, 0);
        OP_WRITE(BOX_ML);
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
        OP_WRITE(BOX_MR);

        /* Draw all list rows */
        for (int r = 0; r < st->visible_rows; r++) {
            int idx = st->scroll_top + r;
            draw_list_row(st, r, idx, idx == st->selected);
        }

        /* Draw bottom border */
        OP_CURSOR(st->top_row + 3 + st->visible_rows, 0);
        OP_WRITE(BOX_BL);
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
        OP_WRITE(BOX_BR);

        /* Draw scrollbar */
        draw_scrollbar(st);
        
        st->last_selected = st->selected;
        st->last_scroll_top = st->scroll_top;
        return;
    }

    /* Incremental redraw */
    if (st->scroll_top != st->last_scroll_top) {
        /* Scroll changed - redraw all visible rows */
        for (int r = 0; r < st->visible_rows; r++) {
            int idx = st->scroll_top + r;
            draw_list_row(st, r, idx, idx == st->selected);
        }
        draw_scrollbar(st);
    } else if (st->selected != st->last_selected) {
        /* Only selection changed - redraw old and new rows */
        int old_row = st->last_selected - st->scroll_top;
        int new_row = st->selected - st->scroll_top;
        
        if (old_row >= 0 && old_row < st->visible_rows) {
            draw_list_row(st, old_row, st->last_selected, false);
        }
        if (new_row >= 0 && new_row < st->visible_rows) {
            draw_list_row(st, new_row, st->selected, true);
        }
    }
    
    st->last_selected = st->selected;
    st->last_scroll_top = st->scroll_top;
}

static void nav_draw(NavState *st)
{
    int w = st->box_width;
    int inner = w - 2;
    int i;

    OP_CURSOR(st->top_row, 0);
    OP_WRITE(BOX_TL);
    for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    OP_WRITE(BOX_TR);

    OP_CURSOR(st->top_row + 1, 0);
    OP_WRITE(BOX_V);
    if (OP_ANSI) OP_WRITE(ANSI_BOLD ANSI_CYAN);
    OP_WRITE_PAD("  NCD Navigator  (Up/Down=siblings  Tab=child  Left=parent  Right=enter  Backspace=Back  Enter=OK  Esc=Cancel)", inner);
    if (OP_ANSI) OP_WRITE(ANSI_RESET);
    OP_WRITE(BOX_V);

    OP_CURSOR(st->top_row + 2, 0);
    OP_WRITE(BOX_V);
    { char line[512]; snprintf(line, sizeof(line), "  Parent : %.*s", (int)(sizeof(line) - 13), st->has_parent ? st->parent_path : "(root)"); OP_WRITE_PAD(line, inner); }
    OP_WRITE(BOX_V);

    OP_CURSOR(st->top_row + 3, 0);
    OP_WRITE(BOX_V);
    { char line[512]; snprintf(line, sizeof(line), "  Current: %.*s", (int)(sizeof(line) - 13), st->current_path); OP_WRITE_PAD(line, inner); }
    OP_WRITE(BOX_V);

    OP_CURSOR(st->top_row + 4, 0);
    OP_WRITE(BOX_ML);
    for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    OP_WRITE(BOX_MR);

    int row = st->top_row + 5;
    for (int r = 0; r < st->visible_rows; r++) {
        int idx = st->sibling_scroll + r;
        OP_CURSOR(row + r, 0);
        OP_WRITE(BOX_V);
        char line[512];
        if (idx < st->siblings.count) {
            bool is_sel = (idx == st->sibling_sel);
            snprintf(line, sizeof(line), "  %c %s", is_sel ? '>' : ' ', st->siblings.items[idx]);
            if (is_sel) con_set_highlight(true);
            OP_WRITE_PAD(line, inner);
            if (is_sel) con_set_highlight(false);
        } else { line[0] = '\0'; OP_WRITE_PAD(line, inner); }
        OP_WRITE(BOX_V);
    }

    row += st->visible_rows;
    OP_CURSOR(row, 0);
    OP_WRITE(BOX_ML);
    for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    OP_WRITE(BOX_MR);

    row++;
    for (int r = 0; r < st->visible_rows; r++) {
        int idx = st->child_scroll + r;
        OP_CURSOR(row + r, 0);
        OP_WRITE(BOX_V);
        char line[512];
        if (idx < st->children.count) {
            bool is_sel = (idx == st->child_sel);
            snprintf(line, sizeof(line), "  %c child: %s", is_sel ? '>' : ' ', st->children.items[idx]);
            if (is_sel) con_set_highlight(true);
            OP_WRITE_PAD(line, inner);
            if (is_sel) con_set_highlight(false);
        } else { line[0] = '\0'; OP_WRITE_PAD(line, inner); }
        OP_WRITE(BOX_V);
    }

    OP_CURSOR(row + st->visible_rows, 0);
    OP_WRITE(BOX_BL);
    for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    OP_WRITE(BOX_BR);
}

static bool nav_reload_lists(NavState *st)
{
    names_free(&st->siblings);
    names_free(&st->children);
    st->has_parent = path_parent(st->current_path, st->parent_path, sizeof(st->parent_path));

    if (st->has_parent) {
        if (!con_list_subdirs(st->parent_path, &st->siblings)) return false;
    } else {
        if (!names_push(&st->siblings, st->current_path)) return false;
    }
    if (!con_list_subdirs(st->current_path, &st->children)) return false;

    const char *leaf = path_leaf(st->current_path);
    st->sibling_sel = 0;
    for (int i = 0; i < st->siblings.count; i++) {
        if (_stricmp(st->siblings.items[i], leaf) == 0) {
            st->sibling_sel = i;
            break;
        }
    }
    st->sibling_scroll = 0;
    st->child_sel = 0;
    st->child_scroll = 0;
    nav_clamp_scroll(st);
    return true;
}

/* ======================================================================== */
/* ========================== Public UI functions ========================== */
/* ======================================================================== */

static int navigate_run(const char *start_path,
                        char       *out_path,
                        size_t      out_path_size,
                        bool        allow_back)
{
    int nav_result = NAV_RESULT_CANCEL;
    int cols, rows, cur_row;
    OP_SIZE(&cols, &rows, &cur_row);

    int bw = cols; if (bw > 120) bw = 120; if (bw < 40) bw = 40;
    int max_list = (rows - 10) / 2;
    if (max_list < 3) max_list = 3;
    if (max_list > 10) max_list = 10;

    int total_rows = 7 + (max_list * 2);
    for (int i = 0; i < total_rows; i++) OP_WRITELN("");

    /* Re-query cursor after printing newlines */
    OP_SIZE(&cols, &rows, &cur_row);
    int box_top = cur_row - total_rows;
    if (box_top < 0) box_top = 0;

    NavState st;
    memset(&st, 0, sizeof(st));
    st.top_row = box_top;
    st.box_width = bw;
    st.visible_rows = max_list;
    platform_strncpy_s(st.current_path, sizeof(st.current_path), start_path);

    if (!nav_reload_lists(&st)) {
        names_free(&st.siblings); names_free(&st.children);
        return NAV_RESULT_CANCEL;
    }

    OP_HIDE();
    nav_draw(&st);

    for (;;) {
        int key = g_ui_ops->read_key();
        if (key == KEY_ENTER) {
                platform_strncpy_s(out_path, out_path_size, st.current_path);
            nav_result = NAV_RESULT_SELECT;
            break;
        }
        if (key == KEY_ESC) { nav_result = NAV_RESULT_CANCEL; break; }
        if (key == KEY_BACKSPACE && allow_back) { nav_result = NAV_RESULT_BACK; break; }

        if (key == KEY_UP) {
            if (st.sibling_sel > 0) st.sibling_sel--;
        } else if (key == KEY_DOWN) {
            if (st.sibling_sel + 1 < st.siblings.count) st.sibling_sel++;
        } else if (key == KEY_LEFT) {
            if (st.has_parent) {
                platform_strncpy_s(st.current_path, sizeof(st.current_path), st.parent_path);
                if (!nav_reload_lists(&st)) break;
            }
        } else if (key == KEY_RIGHT) {
            if (st.children.count > 0) {
                char next[MAX_PATH];
                if (path_join(next, sizeof(next), st.current_path, st.children.items[st.child_sel])) {
                    platform_strncpy_s(st.current_path, sizeof(st.current_path), next);
                    if (!nav_reload_lists(&st)) break;
                }
            }
        } else if (key == KEY_TAB) {
            if (st.children.count > 0)
                st.child_sel = (st.child_sel + 1) % st.children.count;
        } else if (key == KEY_HOME) {
            st.sibling_sel = 0;
        } else if (key == KEY_END) {
            if (st.siblings.count > 0) st.sibling_sel = st.siblings.count - 1;
        } else {
            continue;
        }

        if (st.siblings.count > 0 && st.sibling_sel < st.siblings.count) {
            char candidate[MAX_PATH];
            const char *base = st.has_parent ? st.parent_path : st.current_path;
            if (st.has_parent &&
                path_join(candidate, sizeof(candidate), base, st.siblings.items[st.sibling_sel])) {
                if (_stricmp(candidate, st.current_path) != 0) {
                    platform_strncpy_s(st.current_path, sizeof(st.current_path), candidate);
                    if (!nav_reload_lists(&st)) break;
                }
            }
        }

        nav_clamp_scroll(&st);
        nav_draw(&st);
    }

    OP_CLEAR(box_top, total_rows, bw);
    OP_CURSOR(box_top, 0);
    names_free(&st.siblings);
    names_free(&st.children);
    return nav_result;
}

int ui_select_match_ex(const NcdMatch *matches, int count,
                       const char *search_str,
                       char       *out_path,
                       size_t      out_path_size)
{
    if (!matches || count <= 0) return -1;
    if (count == 1) return 0;

    UiIoOps *ops = ui_get_io_backend();
    ops->open_console();

    int cols, rows, cur_row;
    OP_SIZE(&cols, &rows, &cur_row);

    int max_list = rows - 6;
    if (max_list < 3) max_list = 3;
    if (max_list > 20) max_list = 20;
    int visible = count < max_list ? count : max_list;

    int bw = cols; if (bw > 120) bw = 120; if (bw < 40) bw = 40;
    int total_rows = 4 + visible;
    for (int i = 0; i < total_rows; i++) OP_WRITELN("");

    OP_SIZE(&cols, &rows, &cur_row);
    int box_top = cur_row - total_rows;
    if (box_top < 0) box_top = 0;

    /* Allocate filtered index array */
    int *filtered = (int *)malloc(sizeof(int) * count);
    if (!filtered) { ops->close_console(); return 0; }

    UiState st = {
        .top_row      = box_top,
        .box_width    = bw,
        .visible_rows = visible,
        .count        = count,
        .selected     = 0,
        .scroll_top   = 0,
        .matches      = matches,
        .search_str   = search_str,
        .last_selected = -1,
        .last_scroll_top = -1,
        .first_draw   = true,
        .filter       = {0},
        .filter_len   = 0,
        .filtered     = filtered,
        .filtered_count = count
    };

    /* Initialize identity mapping */
    for (int i = 0; i < count; i++) filtered[i] = i;

    OP_HIDE();
    draw_box(&st);

    int result = -1;
    for (;;) {
        int key = ops->read_key();
        switch (key) {
            case KEY_UP:
                if (st.selected > 0) st.selected--;
                break;
            case KEY_DOWN:
                if (st.selected < st.filtered_count - 1) st.selected++;
                break;
            case KEY_PGUP:
                st.selected -= st.visible_rows;
                if (st.selected < 0) st.selected = 0;
                break;
            case KEY_PGDN:
                st.selected += st.visible_rows;
                if (st.selected >= st.filtered_count) st.selected = st.filtered_count - 1;
                break;
            case KEY_HOME:
                st.selected = 0;
                break;
            case KEY_END:
                if (st.filtered_count > 0) st.selected = st.filtered_count - 1;
                break;
            case KEY_BACKSPACE:
                if (st.filter_len > 0) {
                    st.filter[--st.filter_len] = '\0';
                    apply_filter(&st);
                    /* Recalculate visible rows */
                    int new_vis = st.filtered_count < max_list ? st.filtered_count : max_list;
                    if (new_vis < 1) new_vis = 1;
                    /* Resize box if needed */
                    if (new_vis != st.visible_rows) {
                        OP_CLEAR(box_top, total_rows, bw);
                        st.visible_rows = new_vis;
                        total_rows = 4 + new_vis;
                    }
                    draw_box(&st);
                }
                break;
            case KEY_TAB: {
                if (st.filtered_count == 0) break;
                int real_idx = st.filtered[st.selected];
                char nav_out[MAX_PATH] = {0};
                OP_CLEAR(box_top, total_rows, bw);
                OP_CURSOR(box_top, 0);
                int nav_result = navigate_run(matches[real_idx].full_path,
                                              nav_out, sizeof(nav_out), true);
                if (nav_result == NAV_RESULT_BACK) { draw_box(&st); continue; }
                if (nav_result == NAV_RESULT_SELECT && out_path && out_path_size > 0) {
                    platform_strncpy_s(out_path, out_path_size, nav_out);
                    result = -2;
                } else { result = -1; }
                goto done;
            }
            case KEY_ENTER:
                if (st.filtered_count > 0) {
                    result = st.filtered[st.selected];
                } else {
                    result = -1;
                }
                goto done;
            case KEY_ESC:
                if (st.filter_len > 0) {
                    /* First Escape: clear filter */
                    st.filter_len = 0;
                    st.filter[0] = '\0';
                    apply_filter(&st);
                    int new_vis = st.filtered_count < max_list ? st.filtered_count : max_list;
                    if (new_vis != st.visible_rows) {
                        OP_CLEAR(box_top, total_rows, bw);
                        st.visible_rows = new_vis;
                        total_rows = 4 + new_vis;
                    }
                    draw_box(&st);
                    break;
                }
                result = -1;
                goto done;
            default:
                /* Printable character: append to filter */
                if (key >= 32 && key <= 126 && st.filter_len < 63) {
                    st.filter[st.filter_len++] = (char)key;
                    st.filter[st.filter_len] = '\0';
                    apply_filter(&st);
                    int new_vis = st.filtered_count < max_list ? st.filtered_count : max_list;
                    if (new_vis < 1) new_vis = 1;
                    if (new_vis != st.visible_rows) {
                        OP_CLEAR(box_top, total_rows, bw);
                        st.visible_rows = new_vis;
                        total_rows = 4 + new_vis;
                    }
                    draw_box(&st);
                }
                break;
        }
        clamp_scroll(&st);
        draw_box(&st);
    }

done:
    free(filtered);
    OP_CLEAR(box_top, total_rows, bw);
    OP_CURSOR(box_top, 0);
    OP_SHOW();
    ops->close_console();
    return result;
}

int ui_select_match(const NcdMatch *matches, int count, const char *search_str)
{
    return ui_select_match_ex(matches, count, search_str, NULL, 0);
}

/*
 * Helper macros and types for history selection UI
 */
#define HISTORY_CLAMP(s, max_list_val) do { \
    if ((s)->selected < 0) (s)->selected = 0; \
    if ((s)->selected >= (s)->count) (s)->selected = (s)->count - 1; \
    if ((s)->scroll_top > (s)->selected) (s)->scroll_top = (s)->selected; \
    if ((s)->scroll_top + (s)->visible_rows <= (s)->selected) \
        (s)->scroll_top = (s)->selected - (s)->visible_rows + 1; \
    if ((s)->scroll_top + (s)->visible_rows > (s)->count) \
        (s)->scroll_top = (s)->count - (s)->visible_rows; \
    if ((s)->scroll_top < 0) (s)->scroll_top = 0; \
    if ((s)->visible_rows > (s)->count) (s)->visible_rows = (s)->count; \
    if ((s)->visible_rows < 1) (s)->visible_rows = 1; \
} while (0)

typedef struct {
    int top_row;
    int box_width;
    int visible_rows;
    int count;
    int selected;
    int scroll_top;
    NcdMatch *matches;
} HistoryUiState;

static void history_clear_area(int start_row, int num_rows)
{
    for (int r = 0; r < num_rows; r++) {
        OP_CURSOR(start_row + r, 0);
#if NCD_PLATFORM_WINDOWS
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            DWORD written;
            FillConsoleOutputCharacterA(h, ' ', csbi.dwSize.X - csbi.dwCursorPosition.X, csbi.dwCursorPosition, &written);
        }
#else
        printf("\033[K");
#endif
    }
}

static void history_draw_row(HistoryUiState *s, int row, int idx, int list_start, int inner)
{
    bool is_selected = (idx == s->selected);

    OP_CURSOR(list_start + row, 0);
    OP_WRITE(BOX_V);

    char line[256];
    const char *path = s->matches[idx].full_path;
    int num_width = 4;  /* " 1) " */
    int avail = inner - num_width - 1;

    if ((int)strlen(path) > avail) {
        int skip = (int)strlen(path) - avail + 3;
        snprintf(line, sizeof(line), " %d) ...%s", idx + 1, path + skip);
    } else {
        snprintf(line, sizeof(line), " %d) %s", idx + 1, path);
    }

    if (is_selected) con_set_highlight(true);
    OP_WRITE_PAD(line, inner);
    if (is_selected) con_set_highlight(false);
    OP_WRITE(BOX_V);
}

static void history_draw_box(HistoryUiState *s, int inner, int list_start, const char *title)
{
    /* Title bar */
    OP_CURSOR(s->top_row, 0);
    OP_WRITE(BOX_TL);
    {
        int title_len = (int)strlen(title);
        int left_pad = (inner - title_len) / 2;
        int right_pad = inner - title_len - left_pad;
        if (left_pad < 0) left_pad = 0;
        if (right_pad < 0) right_pad = 0;
        for (int i = 0; i < left_pad; i++) OP_WRITE(BOX_H);
        OP_WRITE(title);
        for (int i = 0; i < right_pad; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_TR);

    /* Help text */
    OP_CURSOR(s->top_row + 1, 0);
    OP_WRITE(BOX_V);
    const char *help = " Enter=select, Del=remove, Esc=cancel ";
    OP_WRITE_PAD(help, inner);
    OP_WRITE(BOX_V);

    /* Separator */
    OP_CURSOR(s->top_row + 2, 0);
    OP_WRITE(BOX_ML);
    for (int i = 0; i < inner; i++) OP_WRITE(BOX_H);
    OP_WRITE(BOX_MR);

    /* List rows */
    for (int i = 0; i < s->visible_rows; i++) {
        int idx = s->scroll_top + i;
        if (idx < s->count) {
            history_draw_row(s, i, idx, list_start, inner);
        } else {
            OP_CURSOR(list_start + i, 0);
            OP_WRITE(BOX_V);
            OP_WRITE_PAD("", inner);
            OP_WRITE(BOX_V);
        }
    }

    /* Bottom border */
    OP_CURSOR(s->top_row + 3 + s->visible_rows, 0);
    OP_WRITE(BOX_BL);
    for (int i = 0; i < inner; i++) OP_WRITE(BOX_H);
    OP_WRITE(BOX_BR);
}

/*
 * History selection UI with Delete key support.
 * Similar to ui_select_match_ex but allows removing entries with Delete key.
 * Returns selected index, or -1 if cancelled.
 * The matches array and count may be modified if entries are deleted.
 */
int ui_select_history(NcdMatch *matches, int *count, NcdMetadata *meta,
                      ui_history_delete_cb delete_callback, void *user_data)
{
    if (!matches || !count || *count <= 0) return -1;
    if (*count == 1) return 0;

    UiIoOps *ops = ui_get_io_backend();
    ops->open_console();

    int cols, rows, cur_row;
    OP_SIZE(&cols, &rows, &cur_row);

    int max_list = rows - 6;
    if (max_list < 3) max_list = 3;
    if (max_list > 20) max_list = 20;
    int visible = *count < max_list ? *count : max_list;

    int bw = cols; if (bw > 120) bw = 120; if (bw < 40) bw = 40;
    int total_rows = 4 + visible;
    for (int i = 0; i < total_rows; i++) OP_WRITELN("");

    OP_SIZE(&cols, &rows, &cur_row);
    int box_top = cur_row - total_rows;
    if (box_top < 0) box_top = 0;

    HistoryUiState st;
    st.top_row = box_top;
    st.box_width = bw;
    st.visible_rows = visible;
    st.count = *count;
    st.selected = 0;
    st.scroll_top = 0;
    st.matches = matches;

    int inner = bw - 2;
    int list_start = st.top_row + 3;
    const char *title = "History (Del to remove)";

    OP_HIDE();
    history_draw_box(&st, inner, list_start, title);

    int result = -1;
    for (;;) {
        int key = ops->read_key();
        switch (key) {
            case KEY_UP:
                if (st.selected > 0) st.selected--;
                break;
            case KEY_DOWN:
                if (st.selected < st.count - 1) st.selected++;
                break;
            case KEY_PGUP:
                st.selected -= st.visible_rows;
                if (st.selected < 0) st.selected = 0;
                break;
            case KEY_PGDN:
                st.selected += st.visible_rows;
                if (st.selected >= st.count) st.selected = st.count - 1;
                break;
            case KEY_HOME: st.selected = 0; break;
            case KEY_END:  st.selected = st.count - 1; break;
            case KEY_ENTER:
                result = st.selected;
                goto history_done;
            case KEY_ESC:
                result = -1;
                goto history_done;
            case KEY_DELETE: {
                if (st.count <= 0) break;

                /* Find and remove this entry from persistent history */
                const char *path = st.matches[st.selected].full_path;
                for (int i = 0; i < db_dir_history_count(meta); i++) {
                    const NcdDirHistoryEntry *e = db_dir_history_get(meta, i);
                    if (e && _stricmp(e->path, path) == 0) {
                        if (delete_callback) {
                            /* Use callback (service mode) */
                            delete_callback(i, e->path, user_data);
                        } else {
                            /* Direct modification (standalone mode) */
                            db_dir_history_remove(meta, i);
                            db_metadata_save(meta);
                        }
                        break;
                    }
                }

                /* Remove from display array */
                memmove(&st.matches[st.selected], &st.matches[st.selected + 1],
                        (size_t)(st.count - st.selected - 1) * sizeof(NcdMatch));
                st.count--;
                (*count)--;

                /* Handle empty list */
                if (st.count == 0) {
                    result = -1;
                    goto history_done;
                }

                /* Adjust selection */
                if (st.selected >= st.count) st.selected = st.count - 1;

                /* Recalculate visible rows and redraw */
                st.visible_rows = st.count < max_list ? st.count : max_list;
                HISTORY_CLAMP(&st, max_list);

                /* Full redraw needed since list changed */
                history_clear_area(st.top_row, total_rows);
                total_rows = 4 + st.visible_rows;
                history_draw_box(&st, inner, list_start, title);
                continue;
            }
            default: break;
        }
        HISTORY_CLAMP(&st, max_list);
        history_draw_box(&st, inner, list_start, title);
    }

history_done:
    OP_SHOW();
    ops->close_console();
    return result;
}

bool ui_navigate_directory(const char *start_path,
                           char       *out_path,
                           size_t      out_path_size)
{
    if (!start_path || !start_path[0] || !out_path || out_path_size == 0) return false;
    UiIoOps *ops = ui_get_io_backend();
    ops->open_console();
    OP_HIDE();
    int nav = navigate_run(start_path, out_path, out_path_size, false);
    OP_SHOW();
    ops->close_console();
    return nav == NAV_RESULT_SELECT;
}

/* ======================================================================== */
/* ===================== Drive Update Selection UI ======================== */
/* ======================================================================== */

typedef struct {
    int top_row;
    int box_width;
    int visible_rows;
    int count;
    int selected;      /* Currently highlighted row */
    int scroll_top;    /* First visible row */
    bool first_draw;
} DriveUiState;

static void draw_drive_box(DriveUiState *st, const char *drives,
                           const uint16_t *versions, bool *selected_drives)
{
    int w = st->box_width;
    int inner = w - 2;
    int list_start = st->top_row + 3;
    
    /* Title bar */
    OP_CURSOR(st->top_row, 0);
    OP_WRITE(BOX_TL);
    {
        char title[128];
        snprintf(title, sizeof(title), " NCD Database Update Required ");
        int title_len = (int)strlen(title);
        int left_pad = (inner - title_len) / 2;
        int right_pad = inner - title_len - left_pad;
        int i;
        for (i = 0; i < left_pad; i++) OP_WRITE(BOX_H);
        OP_WRITE(title);
        for (i = 0; i < right_pad; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_TR);
    
    /* Help text */
    OP_CURSOR(st->top_row + 1, 0);
    OP_WRITE(BOX_V);
    {
        char help[256];
        snprintf(help, sizeof(help), " %-3s | %-7s | %s",
                 "Sel", "Version", "Drive");
        OP_WRITE_PAD(help, inner);
    }
    OP_WRITE(BOX_V);
    
    /* Separator line */
    OP_CURSOR(st->top_row + 2, 0);
    OP_WRITE(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_MR);
    
    /* Drive list */
    for (int row = 0; row < st->visible_rows; row++) {
        int idx = st->scroll_top + row;
        OP_CURSOR(list_start + row, 0);
        OP_WRITE(BOX_V);
        
        if (idx < st->count) {
            bool is_selected_row = (idx == st->selected);
            bool is_checked = selected_drives[idx];
            
            if (is_selected_row) con_set_highlight(true);
            
            char line[512];
            snprintf(line, sizeof(line), " [%c] | v%-5u | %c:",
                     is_checked ? 'X' : ' ',
                     versions[idx],
                     drives[idx]);
            OP_WRITE_PAD(line, inner);
            
            if (is_selected_row) con_set_highlight(false);
        } else {
            OP_WRITE_PAD("", inner);
        }
        OP_WRITE(BOX_V);
    }
    
    /* Footer with instructions */
    int footer_row = list_start + st->visible_rows;
    OP_CURSOR(footer_row, 0);
    OP_WRITE(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_MR);
    
    OP_CURSOR(footer_row + 1, 0);
    OP_WRITE(BOX_V);
    {
        char footer[256];
        snprintf(footer, sizeof(footer), " SPACE=Toggle  A=All  N=None  ENTER=Update Selected  ESC=Skip All ");
        int pad = inner - (int)strlen(footer);
        OP_WRITE(footer);
        int i;
        for (i = 0; i < pad; i++) OP_WRITE(" ");
    }
    OP_WRITE(BOX_V);
    
    OP_CURSOR(footer_row + 2, 0);
    OP_WRITE(BOX_BL);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_BR);
}

static void drive_clamp_scroll(DriveUiState *st)
{
    if (st->selected < 0) st->selected = 0;
    if (st->selected >= st->count) st->selected = st->count - 1;
    if (st->selected < st->scroll_top)
        st->scroll_top = st->selected;
    if (st->selected >= st->scroll_top + st->visible_rows)
        st->scroll_top = st->selected - st->visible_rows + 1;
    if (st->scroll_top < 0) st->scroll_top = 0;
    if (st->count <= st->visible_rows) st->scroll_top = 0;
}

int ui_select_drives_for_update(const char *drives,
                                const uint16_t *versions,
                                int count,
                                bool *selected,
                                int max_width)
{
    if (!drives || !versions || !selected || count <= 0) return UI_UPDATE_NONE;
    
    UiIoOps *ops = ui_get_io_backend();
    ops->open_console();
    
    /* Initialize all as selected */
    for (int i = 0; i < count; i++) selected[i] = true;
    
    int cols, rows, cur_row;
    OP_SIZE(&cols, &rows, &cur_row);
    
    int bw = cols;
    if (bw > max_width) bw = max_width;
    if (bw < 50) bw = 50;
    
    int max_list = rows - 8;
    if (max_list < 3) max_list = 3;
    if (max_list > 20) max_list = 20;
    int visible = count < max_list ? count : max_list;
    
    int total_rows = 5 + visible;
    for (int i = 0; i < total_rows; i++) OP_WRITELN("");
    
    OP_SIZE(&cols, &rows, &cur_row);
    int box_top = cur_row - total_rows;
    if (box_top < 0) box_top = 0;
    
    DriveUiState st = {
        .top_row = box_top,
        .box_width = bw,
        .visible_rows = visible,
        .count = count,
        .selected = 0,
        .scroll_top = 0,
        .first_draw = true
    };
    
    OP_HIDE();
    draw_drive_box(&st, drives, versions, selected);
    
    int result = UI_UPDATE_NONE;
    
    for (;;) {
        int key = ops->read_key();
        switch (key) {
            case KEY_UP:
                if (st.selected > 0) st.selected--;
                break;
            case KEY_DOWN:
                if (st.selected < count - 1) st.selected++;
                break;
            case KEY_HOME:
                st.selected = 0;
                break;
            case KEY_END:
                st.selected = count - 1;
                break;
            case KEY_PGUP:
                st.selected -= visible;
                if (st.selected < 0) st.selected = 0;
                break;
            case KEY_PGDN:
                st.selected += visible;
                if (st.selected >= count) st.selected = count - 1;
                break;
            case ' ':
                /* Toggle selection */
                selected[st.selected] = !selected[st.selected];
                break;
            case 'a':
            case 'A':
                /* Select all */
                for (int i = 0; i < count; i++) selected[i] = true;
                break;
            case 'n':
            case 'N':
                /* Select none */
                for (int i = 0; i < count; i++) selected[i] = false;
                break;
            case KEY_ENTER:
                /* Check if any are selected */
                {
                    bool any = false;
                    for (int i = 0; i < count; i++) {
                        if (selected[i]) { any = true; break; }
                    }
                    if (any) {
                        /* Check if all are selected */
                        bool all = true;
                        for (int i = 0; i < count; i++) {
                            if (!selected[i]) { all = false; break; }
                        }
                        result = all ? UI_UPDATE_ALL : UI_UPDATE_SOME;
                    } else {
                        result = UI_UPDATE_NONE;
                    }
                }
                goto drive_done;
            case KEY_ESC:
                /* Cancel - skip all */
                for (int i = 0; i < count; i++) selected[i] = false;
                result = UI_UPDATE_NONE;
                goto drive_done;
            default:
                continue;
        }
        drive_clamp_scroll(&st);
        draw_drive_box(&st, drives, versions, selected);
    }
    
drive_done:
    OP_CLEAR(box_top, total_rows, bw);
    OP_CURSOR(box_top, 0);
    OP_SHOW();
    ops->close_console();
    return result;
}

/* ======================================================================== */
/* ===================== Configuration Editor UI ========================= */
/* ======================================================================== */

typedef struct {
    int top_row;
    int box_width;
    int visible_rows;
    int count;
    int selected;
    bool first_draw;
    /* Input mode for numeric fields */
    bool input_mode;
    char input_buf[16];
    int input_len;
    int input_orig_value;
} ConfigUiState;

typedef struct {
    const char *label;
    bool *value;
    bool is_bool;      /* true for bool, false for int */
    int *int_value;
    int min_val;
    int max_val;
    const char *fmt;   /* format string for display */
} ConfigItem;

static void draw_config_box(ConfigUiState *st, ConfigItem *items, int item_count,
                            NcdMetadata *meta)
{
    (void)meta; /* meta is passed for future use but currently not needed */
    int w = st->box_width;
    int inner = w - 2;
    int list_start = st->top_row + 3;
    
    /* Title bar */
    OP_CURSOR(st->top_row, 0);
    OP_WRITE(BOX_TL);
    {
        char title[128];
        snprintf(title, sizeof(title), " NCD Configuration ");
        int title_len = (int)strlen(title);
        int left_pad = (inner - title_len) / 2;
        int right_pad = inner - title_len - left_pad;
        int i;
        for (i = 0; i < left_pad; i++) OP_WRITE(BOX_H);
        OP_WRITE(title);
        for (i = 0; i < right_pad; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_TR);
    
    /* Help text */
    OP_CURSOR(st->top_row + 1, 0);
    OP_WRITE(BOX_V);
    if (st->input_mode) {
        OP_WRITE_PAD(" Enter/Tab=Confirm  Esc=Cancel ", inner);
    } else {
        OP_WRITE_PAD(" UP/DOWN=Navigate  SPACE=Toggle  +/- or 0-9=Adjust  X=Exclusions ", inner);
    }
    OP_WRITE(BOX_V);
    
    /* Separator line */
    OP_CURSOR(st->top_row + 2, 0);
    OP_WRITE(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_MR);
    
    /* Config items */
    for (int row = 0; row < st->visible_rows; row++) {
        int idx = row;
        OP_CURSOR(list_start + row, 0);
        OP_WRITE(BOX_V);
        
        if (idx < item_count) {
            bool is_selected_row = (idx == st->selected);
            ConfigItem *item = &items[idx];
            
            if (is_selected_row) con_set_highlight(true);
            
            char line[512];
            if (item->is_bool) {
                snprintf(line, sizeof(line), " %s: [%s] ",
                         item->label,
                         *item->value ? "X" : " ");
            } else {
                /* In input mode, show the input buffer for the selected item */
                if (is_selected_row && st->input_mode) {
                    snprintf(line, sizeof(line), " %s: [%s] ",
                             item->label, st->input_buf);
                } else {
                    /* Special display for rescan interval (item 5) */
                    if (idx == 5 && *item->int_value == -1) {
                        snprintf(line, sizeof(line), " %s: Never ",
                                 item->label);
                    } else {
                        snprintf(line, sizeof(line), " %s: %d ",
                                 item->label,
                                 *item->int_value);
                    }
                }
            }
            OP_WRITE_PAD(line, inner);
            
            if (is_selected_row) con_set_highlight(false);
        } else {
            OP_WRITE_PAD("", inner);
        }
        OP_WRITE(BOX_V);
    }
    
    /* Footer with instructions */
    int footer_row = list_start + st->visible_rows;
    OP_CURSOR(footer_row, 0);
    OP_WRITE(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_MR);
    
    OP_CURSOR(footer_row + 1, 0);
    OP_WRITE(BOX_V);
    {
        char footer[256];
        if (st->input_mode) {
            if (st->selected == 3) {
                snprintf(footer, sizeof(footer), " Type timeout value (10-3600s) ");
            } else if (st->selected == 4) {
                snprintf(footer, sizeof(footer), " Type retry count (0-255, 0=default) ");
            } else if (st->selected == 5) {
                snprintf(footer, sizeof(footer), " Type rescan hours (-1=never, 1-168) ");
            } else {
                snprintf(footer, sizeof(footer), " Type value ");
            }
        } else {
            snprintf(footer, sizeof(footer), " ENTER=Save  ESC=Cancel ");
        }
        int pad = inner - (int)strlen(footer);
        OP_WRITE(footer);
        int i;
        for (i = 0; i < pad; i++) OP_WRITE(" ");
    }
    OP_WRITE(BOX_V);
    
    OP_CURSOR(footer_row + 2, 0);
    OP_WRITE(BOX_BL);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_BR);
}

bool ui_edit_config(NcdMetadata *meta)
{
    if (!meta) return false;
    
    NcdConfig *cfg = &meta->cfg;
    
    /* Initialize defaults if not already set */
    if (cfg->magic != NCD_CFG_MAGIC) {
        db_config_init_defaults(cfg);
    }
    
    UiIoOps *ops = ui_get_io_backend();
    ops->open_console();
    
    /* Create working copies of values */
    bool show_hidden = cfg->default_show_hidden;
    bool show_system = cfg->default_show_system;
    bool fuzzy_match = cfg->default_fuzzy_match;
    int timeout = cfg->default_timeout;
    if (timeout < 0) timeout = 300;  /* Default to 300 if not set */
    int service_retry = cfg->service_retry_count;
    if (service_retry == 0) service_retry = NCD_DEFAULT_SERVICE_RETRY_COUNT;
    int rescan_interval = cfg->rescan_interval_hours;
    if (rescan_interval == 0) rescan_interval = NCD_RESCAN_HOURS_DEFAULT;
    
    /* Define config items */
    ConfigItem items[] = {
        {"Show hidden dirs (/i)", &show_hidden, true, NULL, 0, 0, NULL},
        {"Show system dirs (/s)", &show_system, true, NULL, 0, 0, NULL},
        {"Fuzzy matching (/z)", &fuzzy_match, true, NULL, 0, 0, NULL},
        {"Scan timeout in seconds (/t)", NULL, false, &timeout, 10, 3600, NULL},
        {"Service retry count (/retry)", NULL, false, &service_retry, 0, 255, NULL},
        {"Auto-rescan hours (0=never, -1)", NULL, false, &rescan_interval, -1, 168, NULL},
    };
    int item_count = sizeof(items) / sizeof(items[0]);
    
    int cols, rows, cur_row;
    OP_SIZE(&cols, &rows, &cur_row);
    
    int bw = 60;
    if (bw > cols - 4) bw = cols - 4;
    if (bw < 50) bw = 50;
    
    int max_list = rows - 8;
    if (max_list < 3) max_list = 3;
    int visible = item_count < max_list ? item_count : max_list;
    
    int total_rows = 5 + visible;
    for (int i = 0; i < total_rows; i++) OP_WRITELN("");
    
    OP_SIZE(&cols, &rows, &cur_row);
    int box_top = cur_row - total_rows;
    if (box_top < 0) box_top = 0;
    
    ConfigUiState st = {
        .top_row = box_top,
        .box_width = bw,
        .visible_rows = visible,
        .count = item_count,
        .selected = 0,
        .first_draw = true
    };
    
    OP_HIDE();
    draw_config_box(&st, items, item_count, meta);
    
    bool saved = false;
    
    for (;;) {
        int key = ops->read_key();
        
        /* Handle input mode for numeric fields */
        if (st.input_mode) {
            switch (key) {
                case KEY_ENTER:
                case KEY_TAB:
                    /* Confirm input */
                    if (st.input_len > 0) {
                        int new_val = atoi(st.input_buf);
                        ConfigItem *item = &items[st.selected];
                        if (new_val >= item->min_val && new_val <= item->max_val) {
                            *item->int_value = new_val;
                        }
                    }
                    st.input_mode = false;
                    st.input_len = 0;
                    st.input_buf[0] = '\0';
                    break;
                case KEY_ESC:
                    /* Cancel input, restore original value */
                    *items[st.selected].int_value = st.input_orig_value;
                    st.input_mode = false;
                    st.input_len = 0;
                    st.input_buf[0] = '\0';
                    break;
                case KEY_BACKSPACE:
                    /* Remove last digit */
                    if (st.input_len > 0) {
                        st.input_len--;
                        st.input_buf[st.input_len] = '\0';
                    }
                    break;
                default:
                    /* Handle digit input (0-9), and '-' for negative values */
                    if (key >= '0' && key <= '9' && st.input_len < 4) {
                        st.input_buf[st.input_len++] = (char)key;
                        st.input_buf[st.input_len] = '\0';
                    } else if (key == '-' && st.input_len == 0 && st.selected == 5) {
                        /* Allow leading minus sign for rescan interval (item 5) */
                        st.input_buf[st.input_len++] = (char)key;
                        st.input_buf[st.input_len] = '\0';
                    }
                    break;
            }
            draw_config_box(&st, items, item_count, meta);
            continue;
        }
        
        /* Normal navigation mode */
        switch (key) {
            case KEY_UP:
                if (st.selected > 0) st.selected--;
                break;
            case KEY_DOWN:
                if (st.selected < item_count - 1) st.selected++;
                break;
            case KEY_HOME:
                st.selected = 0;
                break;
            case KEY_END:
                st.selected = item_count - 1;
                break;
            case ' ':
                /* Toggle boolean value */
                if (items[st.selected].is_bool) {
                    *items[st.selected].value = !*items[st.selected].value;
                }
                break;
            case KEY_LEFT:
            case '-':
            case '_':
                /* Decrease int value */
                if (!items[st.selected].is_bool) {
                    int *val = items[st.selected].int_value;
                    int min_val = items[st.selected].min_val;
                    if (*val > min_val) (*val)--;
                }
                break;
            case KEY_RIGHT:
            case '+':
            case '=':
                /* Increase int value */
                if (!items[st.selected].is_bool) {
                    int *val = items[st.selected].int_value;
                    int max_val = items[st.selected].max_val;
                    if (*val < max_val) (*val)++;
                }
                break;
            case KEY_ENTER:
                /* Save configuration */
                cfg->default_show_hidden = show_hidden;
                cfg->default_show_system = show_system;
                cfg->default_fuzzy_match = fuzzy_match;
                cfg->default_timeout = timeout;
                cfg->service_retry_count = (uint8_t)service_retry;
                cfg->rescan_interval_hours = (int16_t)rescan_interval;
                cfg->has_defaults = true;
                cfg->magic = NCD_CFG_MAGIC;
                cfg->version = NCD_CFG_VERSION;
                saved = true;
                goto config_done;
            case KEY_ESC:
                /* Cancel */
                saved = false;
                goto config_done;
            case 'x':
            case 'X':
                /* Open exclusion editor */
                if (ui_edit_exclusions(meta)) {
                    /* Exclusions were modified, will be saved with metadata */
                }
                /* Redraw the config box */
                st.first_draw = true;
                break;
            default:
                /* Enter input mode on digit press for non-bool items */
                /* Also allow '-' for rescan interval (item 5) to enter negative values */
                if ((key >= '0' && key <= '9' && !items[st.selected].is_bool) ||
                    (key == '-' && !items[st.selected].is_bool && st.selected == 5)) {
                    st.input_mode = true;
                    st.input_len = 0;
                    st.input_buf[0] = '\0';
                    st.input_orig_value = *items[st.selected].int_value;
                    /* Add the first character */
                    st.input_buf[st.input_len++] = (char)key;
                    st.input_buf[st.input_len] = '\0';
                }
                break;
        }
        draw_config_box(&st, items, item_count, meta);
    }
    
config_done:
    /* Get current console size to clear properly */
    OP_SIZE(&cols, &rows, &cur_row);
    /* Clear total_rows + 1 to include the bottom border row */
    OP_CLEAR(box_top, total_rows + 1, cols);
    OP_CURSOR(box_top, 0);
    OP_SHOW();
    ops->close_console();
    return saved;
}

/* ============================================================= exclusion list editor  */

/* Maximum exclusions to display at once */
#define EXCLUSION_MAX_DISPLAY 20

/* Maximum pattern length for editing */
#define EXCLUSION_MAX_PATTERN 256

typedef struct {
    int top_row;
    int box_width;
    int selected;
    int first_display;
    int count;
    bool editing;
    char edit_buf[EXCLUSION_MAX_PATTERN];
    int edit_len;
    int edit_cursor;
} ExclusionUiState;

static void draw_exclusion_box(ExclusionUiState *st, NcdMetadata *meta)
{
    int cols, rows, cur_row;
    OP_SIZE(&cols, &rows, &cur_row);
    
    int visible = st->count < EXCLUSION_MAX_DISPLAY ? st->count : EXCLUSION_MAX_DISPLAY;
    if (visible < 3) visible = 3;  /* Minimum height */
    
    int inner = st->box_width - 2;
    
    /* Title bar */
    OP_CURSOR(st->top_row, 0);
    OP_WRITE(BOX_TL);
    {
        char title[128];
        snprintf(title, sizeof(title), " Exclusion List ");
        int title_len = (int)strlen(title);
        int left_pad = (inner - title_len) / 2;
        int right_pad = inner - title_len - left_pad;
        int i;
        for (i = 0; i < left_pad; i++) OP_WRITE(BOX_H);
        OP_WRITE(title);
        for (i = 0; i < right_pad; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_TR);
    
    /* Help text */
    OP_CURSOR(st->top_row + 1, 0);
    OP_WRITE(BOX_V);
    if (st->editing) {
        OP_WRITE_PAD(" Enter=Save  Esc=Cancel ", inner);
    } else {
        OP_WRITE_PAD(" A=Add D=Del E=Edit  Enter=Done  Esc=Cancel ", inner);
    }
    OP_WRITE(BOX_V);
    
    /* Separator */
    OP_CURSOR(st->top_row + 2, 0);
    OP_WRITE(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_MR);
    
    /* Draw items */
    for (int i = 0; i < visible; i++) {
        int idx = st->first_display + i;
        int row = st->top_row + 3 + i;
        OP_CURSOR(row, 0);
        OP_WRITE(BOX_V);
        
        if (idx < st->count) {
            NcdExclusionEntry *entry = &meta->exclusions.entries[idx];
            bool is_selected = (idx == st->selected);
            
            if (is_selected) con_set_highlight(true);
            
            /* Format: [drive] pattern */
            char line[256];
            char drive_str[8];
            if (entry->drive == 0) {
                snprintf(drive_str, sizeof(drive_str), "*");
            } else {
                snprintf(drive_str, sizeof(drive_str), "%c:", entry->drive);
            }
            
            snprintf(line, sizeof(line), " %-4s %s", drive_str, entry->pattern);
            
            /* Truncate if too long */
            if ((int)strlen(line) > inner) {
                line[inner - 3] = '.';
                line[inner - 2] = '.';
                line[inner - 1] = '.';
                line[inner] = '\0';
            }
            
            OP_WRITE_PAD(line, inner);
            if (is_selected) con_set_highlight(false);
        } else {
            /* Empty slot */
            OP_WRITE_PAD("", inner);
        }
        OP_WRITE(BOX_V);
    }
    
    /* Bottom border */
    OP_CURSOR(st->top_row + 3 + visible, 0);
    OP_WRITE(BOX_BL);
    {
        int i;
        for (i = 0; i < inner; i++) OP_WRITE(BOX_H);
    }
    OP_WRITE(BOX_BR);
}

bool ui_edit_exclusions(NcdMetadata *meta)
{
    if (!meta) return false;
    
    UiIoOps *ops = ui_get_io_backend();
    ops->open_console();
    
    int cols, rows, cur_row;
    OP_SIZE(&cols, &rows, &cur_row);
    
    ExclusionUiState st = {
        .top_row = cur_row,
        .box_width = 70,
        .selected = 0,
        .first_display = 0,
        .count = meta->exclusions.count,
        .editing = false,
        .edit_len = 0,
        .edit_cursor = 0
    };
    
    if (st.box_width > cols - 4) st.box_width = cols - 4;
    
    bool saved = false;
    bool cancelled = false;
    
    OP_HIDE();
    draw_exclusion_box(&st, meta);
    
    while (!cancelled && !saved) {
        int key = ops->read_key();
        
        if (st.editing) {
            /* Edit mode - handle text input */
            switch (key) {
                case KEY_ENTER:
                    /* Save edited/added pattern */
                    if (st.edit_len > 0) {
                        st.edit_buf[st.edit_len] = '\0';
                        
                        if (st.selected < st.count) {
                            /* Edit existing - remove old, add new */
                            db_exclusion_remove(meta, meta->exclusions.entries[st.selected].pattern);
                            if (db_exclusion_add(meta, st.edit_buf)) {
                                meta->exclusions_dirty = true;
                                st.count = meta->exclusions.count;
                            }
                        } else {
                            /* Add new */
                            if (db_exclusion_add(meta, st.edit_buf)) {
                                meta->exclusions_dirty = true;
                                st.count = meta->exclusions.count;
                                st.selected = st.count - 1;
                            }
                        }
                    }
                    st.editing = false;
                    break;
                    
                case KEY_ESC:
                    /* Cancel edit */
                    st.editing = false;
                    st.edit_len = 0;
                    break;
                    
                case KEY_BACKSPACE:
                    if (st.edit_cursor > 0) {
                        memmove(&st.edit_buf[st.edit_cursor - 1], &st.edit_buf[st.edit_cursor],
                                st.edit_len - st.edit_cursor);
                        st.edit_cursor--;
                        st.edit_len--;
                    }
                    break;
                    
                case KEY_LEFT:
                    if (st.edit_cursor > 0) st.edit_cursor--;
                    break;
                    
                case KEY_RIGHT:
                    if (st.edit_cursor < st.edit_len) st.edit_cursor++;
                    break;
                    
                case KEY_HOME:
                    st.edit_cursor = 0;
                    break;
                    
                case KEY_END:
                    st.edit_cursor = st.edit_len;
                    break;
                    
                case KEY_DELETE:
                    if (st.edit_cursor < st.edit_len) {
                        memmove(&st.edit_buf[st.edit_cursor], &st.edit_buf[st.edit_cursor + 1],
                                st.edit_len - st.edit_cursor - 1);
                        st.edit_len--;
                    }
                    break;
                    
                default:
                    if (key >= 32 && key < 127 && st.edit_len < EXCLUSION_MAX_PATTERN - 1) {
                        memmove(&st.edit_buf[st.edit_cursor + 1], &st.edit_buf[st.edit_cursor],
                                st.edit_len - st.edit_cursor);
                        st.edit_buf[st.edit_cursor] = (char)key;
                        st.edit_cursor++;
                        st.edit_len++;
                    }
                    break;
            }
        } else {
            /* Normal mode - navigation */
            switch (key) {
                case KEY_UP:
                    if (st.selected > 0) {
                        st.selected--;
                        if (st.selected < st.first_display) {
                            st.first_display = st.selected;
                        }
                    }
                    break;
                    
                case KEY_DOWN:
                    if (st.selected < st.count - 1) {
                        st.selected++;
                        int visible = st.count < EXCLUSION_MAX_DISPLAY ? st.count : EXCLUSION_MAX_DISPLAY;
                        if (st.selected >= st.first_display + visible) {
                            st.first_display = st.selected - visible + 1;
                        }
                    }
                    break;
                    
                case KEY_HOME:
                    st.selected = 0;
                    st.first_display = 0;
                    break;
                    
                case KEY_END:
                    st.selected = st.count - 1;
                    if (st.count > EXCLUSION_MAX_DISPLAY) {
                        st.first_display = st.count - EXCLUSION_MAX_DISPLAY;
                    }
                    break;
                    
                case KEY_PGUP:
                    st.selected -= EXCLUSION_MAX_DISPLAY;
                    if (st.selected < 0) st.selected = 0;
                    st.first_display = st.selected;
                    break;
                    
                case KEY_PGDN:
                    st.selected += EXCLUSION_MAX_DISPLAY;
                    if (st.selected >= st.count) st.selected = st.count - 1;
                    if (st.count > EXCLUSION_MAX_DISPLAY) {
                        st.first_display = st.selected - EXCLUSION_MAX_DISPLAY + 1;
                        if (st.first_display < 0) st.first_display = 0;
                    }
                    break;
                    
                case 'a':
                case 'A':
                    /* Add new pattern */
                    st.editing = true;
                    st.edit_len = 0;
                    st.edit_cursor = 0;
                    st.edit_buf[0] = '\0';
                    /* Set selected past end to indicate "add" mode */
                    st.selected = st.count;
                    break;
                    
                case 'd':
                case 'D':
                    /* Delete selected pattern */
                    if (st.selected < st.count) {
                        db_exclusion_remove(meta, meta->exclusions.entries[st.selected].pattern);
                        meta->exclusions_dirty = true;
                        st.count = meta->exclusions.count;
                        if (st.selected >= st.count && st.selected > 0) {
                            st.selected--;
                        }
                        if (st.first_display > 0 && st.count <= EXCLUSION_MAX_DISPLAY) {
                            st.first_display = 0;
                        }
                    }
                    break;
                    
                case 'e':
                case 'E':
                    /* Edit selected pattern */
                    if (st.selected < st.count) {
                        st.editing = true;
                        platform_strncpy_s(st.edit_buf, sizeof(st.edit_buf),
                                          meta->exclusions.entries[st.selected].pattern);
                        st.edit_len = (int)strlen(st.edit_buf);
                        st.edit_cursor = st.edit_len;
                    }
                    break;
                    
                case KEY_ENTER:
                    /* Done - save changes */
                    saved = true;
                    break;
                    
                case KEY_ESC:
                case 'q':
                case 'Q':
                    /* Cancel */
                    cancelled = true;
                    break;
            }
        }
        
        draw_exclusion_box(&st, meta);
    }
    
    /* Cleanup */
    OP_SIZE(&cols, &rows, &cur_row);
    int visible = st.count < EXCLUSION_MAX_DISPLAY ? st.count : EXCLUSION_MAX_DISPLAY;
    if (visible < 3) visible = 3;
    int box_height = visible + 4;
    OP_CLEAR(st.top_row, box_height + 1, cols);
    OP_CURSOR(st.top_row, 0);
    OP_SHOW();
    ops->close_console();
    
    return saved && !cancelled;
}
