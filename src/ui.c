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
    int          count;
    int          selected;
    int          scroll_top;
    const NcdMatch *matches;
    const char  *search_str;
    /* Dirty tracking for incremental redraw */
    int          last_selected;
    int          last_scroll_top;
    bool         first_draw;
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

static void ui_open_console(void)
{
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

static void con_writeln(const char *s) { con_write(s); con_write("\r\n"); }

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

static int read_key(void)
{
    INPUT_RECORD ir;
    DWORD        nread;
    for (;;) {
        if (!ReadConsoleInputA(g_hin, &ir, 1, &nread)) return KEY_ESC;
        if (ir.EventType != KEY_EVENT)       continue;
        if (!ir.Event.KeyEvent.bKeyDown)     continue;
        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
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
            case 'Q': case 'q': return KEY_ESC;
            case ' ':       return ' ';
            default:
                /* Return ASCII digits directly */
                if (vk >= '0' && vk <= '9') return vk;
                continue;
        }
    }
}

static bool list_subdirs(const char *dir_path, NameList *out)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    out->items = NULL; out->count = 0; out->cap = 0;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir_path) >= (int)sizeof(pattern))
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

static void con_writeln(const char *s) { con_write(s); con_write("\n"); }

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

static int read_key(void)
{
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
        case ' ':               return ' ';
        default:
            /* Return ASCII digits directly */
            if (vk >= '0' && vk <= '9') return vk;
            return KEY_UNKNOWN;
    }
}

static bool list_subdirs(const char *dir_path, NameList *out)
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
/* =================== Shared helpers (use con_* from above) =============== */
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

/* Helper: Draw a single list row */
static void draw_list_row(UiState *st, int row, int idx, bool is_selected)
{
    int w = st->box_width;
    int inner = w - 2;
    char line[512];
    int list_start = st->top_row + 3;
    
    con_cursor_pos(list_start + row, 0);
    con_write(BOX_V);
    if (is_selected && g_ansi) con_write(ANSI_REVERSE);
    if (idx < st->count && idx >= 0)
        snprintf(line, sizeof(line), " %s %.*s",
                 is_selected ? SEL_IND : " ",
                 (int)(sizeof(line) - 4), st->matches[idx].full_path);
    else
        line[0] = '\0';
    con_write_padded(line, inner);
    if (is_selected && g_ansi) con_write(ANSI_RESET);
    con_write(BOX_V);
}

/* Helper: Draw scrollbar */
static void draw_scrollbar(UiState *st)
{
    int w = st->box_width;
    int list_start = st->top_row + 3;
    
    if (st->count > st->visible_rows) {
        int track = st->visible_rows;
        int thumb = (int)((double)st->scroll_top / (st->count - st->visible_rows)
                          * (track - 1));
        for (int r = 0; r < track; r++) {
            con_cursor_pos(list_start + r, w - 1);
            con_write(r == thumb ? "#" : ".");
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
        con_cursor_pos(st->top_row, 0);
        con_write(BOX_TL);
        for (i = 0; i < inner; i++) con_write(BOX_H);
        con_write(BOX_TR);

        con_cursor_pos(st->top_row + 1, 0);
        con_write(BOX_V);
        if (g_ansi) con_write(ANSI_BOLD ANSI_CYAN);
        char header[256];
        snprintf(header, sizeof(header),
                 "  NCD  --  %d match%s for \"%s\"  (Up/Dn PgUp PgDn Home End  Tab=Nav  Enter=OK  Esc=Cancel)",
                 st->count, st->count == 1 ? "" : "es", st->search_str);
        con_write_padded(header, inner);
        if (g_ansi) con_write(ANSI_RESET);
        con_write(BOX_V);

        con_cursor_pos(st->top_row + 2, 0);
        con_write(BOX_ML);
        for (i = 0; i < inner; i++) con_write(BOX_H);
        con_write(BOX_MR);

        /* Draw all list rows */
        for (int r = 0; r < st->visible_rows; r++) {
            int idx = st->scroll_top + r;
            draw_list_row(st, r, idx, idx == st->selected);
        }

        /* Draw bottom border */
        con_cursor_pos(st->top_row + 3 + st->visible_rows, 0);
        con_write(BOX_BL);
        for (i = 0; i < inner; i++) con_write(BOX_H);
        con_write(BOX_BR);

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

    con_cursor_pos(st->top_row, 0);
    con_write(BOX_TL);
    for (i = 0; i < inner; i++) con_write(BOX_H);
    con_write(BOX_TR);

    con_cursor_pos(st->top_row + 1, 0);
    con_write(BOX_V);
    if (g_ansi) con_write(ANSI_BOLD ANSI_CYAN);
    con_write_padded("  NCD Navigator  (Up/Down=siblings  Tab=child  Left=parent  Right=enter  Backspace=Back  Enter=OK  Esc=Cancel)", inner);
    if (g_ansi) con_write(ANSI_RESET);
    con_write(BOX_V);

    con_cursor_pos(st->top_row + 2, 0);
    con_write(BOX_V);
    { char line[512]; snprintf(line, sizeof(line), "  Parent : %.*s", (int)(sizeof(line) - 13), st->has_parent ? st->parent_path : "(root)"); con_write_padded(line, inner); }
    con_write(BOX_V);

    con_cursor_pos(st->top_row + 3, 0);
    con_write(BOX_V);
    { char line[512]; snprintf(line, sizeof(line), "  Current: %.*s", (int)(sizeof(line) - 13), st->current_path); con_write_padded(line, inner); }
    con_write(BOX_V);

    con_cursor_pos(st->top_row + 4, 0);
    con_write(BOX_ML);
    for (i = 0; i < inner; i++) con_write(BOX_H);
    con_write(BOX_MR);

    int row = st->top_row + 5;
    for (int r = 0; r < st->visible_rows; r++) {
        int idx = st->sibling_scroll + r;
        con_cursor_pos(row + r, 0);
        con_write(BOX_V);
        char line[512];
        if (idx < st->siblings.count) {
            bool is_sel = (idx == st->sibling_sel);
            snprintf(line, sizeof(line), "  %c %s", is_sel ? '>' : ' ', st->siblings.items[idx]);
            if (is_sel && g_ansi) con_write(ANSI_REVERSE);
            con_write_padded(line, inner);
            if (is_sel && g_ansi) con_write(ANSI_RESET);
        } else { line[0] = '\0'; con_write_padded(line, inner); }
        con_write(BOX_V);
    }

    row += st->visible_rows;
    con_cursor_pos(row, 0);
    con_write(BOX_ML);
    for (i = 0; i < inner; i++) con_write(BOX_H);
    con_write(BOX_MR);

    row++;
    for (int r = 0; r < st->visible_rows; r++) {
        int idx = st->child_scroll + r;
        con_cursor_pos(row + r, 0);
        con_write(BOX_V);
        char line[512];
        if (idx < st->children.count) {
            bool is_sel = (idx == st->child_sel);
            snprintf(line, sizeof(line), "  %c child: %s", is_sel ? '>' : ' ', st->children.items[idx]);
            if (is_sel && g_ansi) con_write(ANSI_REVERSE);
            con_write_padded(line, inner);
            if (is_sel && g_ansi) con_write(ANSI_RESET);
        } else { line[0] = '\0'; con_write_padded(line, inner); }
        con_write(BOX_V);
    }

    con_cursor_pos(row + st->visible_rows, 0);
    con_write(BOX_BL);
    for (i = 0; i < inner; i++) con_write(BOX_H);
    con_write(BOX_BR);
}

static bool nav_reload_lists(NavState *st)
{
    names_free(&st->siblings);
    names_free(&st->children);
    st->has_parent = path_parent(st->current_path, st->parent_path, sizeof(st->parent_path));

    if (st->has_parent) {
        if (!list_subdirs(st->parent_path, &st->siblings)) return false;
    } else {
        if (!names_push(&st->siblings, st->current_path)) return false;
    }
    if (!list_subdirs(st->current_path, &st->children)) return false;

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
    get_console_size(&cols, &rows, &cur_row);

    int bw = cols; if (bw > 120) bw = 120; if (bw < 40) bw = 40;
    int max_list = (rows - 10) / 2;
    if (max_list < 3) max_list = 3;
    if (max_list > 10) max_list = 10;

    int total_rows = 7 + (max_list * 2);
    for (int i = 0; i < total_rows; i++) con_writeln("");

    /* Re-query cursor after printing newlines */
    get_console_size(&cols, &rows, &cur_row);
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

    con_hide_cursor();
    nav_draw(&st);

    for (;;) {
        int key = read_key();
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

    clear_area(box_top, total_rows, bw);
    con_cursor_pos(box_top, 0);
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

    ui_open_console();
#if NCD_PLATFORM_WINDOWS
    if (g_hout == INVALID_HANDLE_VALUE) { ui_close_console(); return 0; }
#else
    if (g_tty_out < 0) { ui_close_console(); return 0; }
#endif

    int cols, rows, cur_row;
    get_console_size(&cols, &rows, &cur_row);

    int max_list = rows - 6;
    if (max_list < 3) max_list = 3;
    if (max_list > 20) max_list = 20;
    int visible = count < max_list ? count : max_list;

    int bw = cols; if (bw > 120) bw = 120; if (bw < 40) bw = 40;
    int total_rows = 4 + visible;
    for (int i = 0; i < total_rows; i++) con_writeln("");

    get_console_size(&cols, &rows, &cur_row);
    int box_top = cur_row - total_rows;
    if (box_top < 0) box_top = 0;

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
        .first_draw   = true
    };

    con_hide_cursor();
    draw_box(&st);

    int result = -1;
    for (;;) {
        int key = read_key();
        switch (key) {
            case KEY_UP:
                if (st.selected > 0) st.selected--;
                break;
            case KEY_DOWN:
                if (st.selected < count - 1) st.selected++;
                break;
            case KEY_PGUP:
                st.selected -= visible;
                if (st.selected < 0) st.selected = 0;
                break;
            case KEY_PGDN:
                st.selected += visible;
                if (st.selected >= count) st.selected = count - 1;
                break;
            case KEY_HOME: st.selected = 0; break;
            case KEY_END:  st.selected = count - 1; break;
            case KEY_TAB: {
                char nav_out[MAX_PATH] = {0};
                clear_area(box_top, total_rows, bw);
                con_cursor_pos(box_top, 0);
                int nav_result = navigate_run(matches[st.selected].full_path,
                                              nav_out, sizeof(nav_out), true);
                if (nav_result == NAV_RESULT_BACK) { draw_box(&st); continue; }
                if (nav_result == NAV_RESULT_SELECT && out_path && out_path_size > 0) {
                platform_strncpy_s(out_path, out_path_size, nav_out);
                    result = -2;
                } else { result = -1; }
                goto done;
            }
            case KEY_ENTER: result = st.selected; goto done;
            case KEY_ESC:   result = -1;          goto done;
            default: continue;
        }
        clamp_scroll(&st);
        draw_box(&st);
    }

done:
    clear_area(box_top, total_rows, bw);
    con_cursor_pos(box_top, 0);
    con_show_cursor();
    ui_close_console();
    return result;
}

int ui_select_match(const NcdMatch *matches, int count, const char *search_str)
{
    return ui_select_match_ex(matches, count, search_str, NULL, 0);
}

bool ui_navigate_directory(const char *start_path,
                           char       *out_path,
                           size_t      out_path_size)
{
    if (!start_path || !start_path[0] || !out_path || out_path_size == 0) return false;
    ui_open_console();
#if NCD_PLATFORM_WINDOWS
    if (g_hout == INVALID_HANDLE_VALUE || g_hin == INVALID_HANDLE_VALUE) {
        ui_close_console(); return false;
    }
#else
    if (g_tty_out < 0 || g_tty_in < 0) { ui_close_console(); return false; }
#endif
    con_hide_cursor();
    int nav = navigate_run(start_path, out_path, out_path_size, false);
    con_show_cursor();
    ui_close_console();
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
    con_cursor_pos(st->top_row, 0);
    con_write(BOX_TL);
    {
        char title[128];
        snprintf(title, sizeof(title), " NCD Database Update Required ");
        int title_len = (int)strlen(title);
        int left_pad = (inner - title_len) / 2;
        int right_pad = inner - title_len - left_pad;
        int i;
        for (i = 0; i < left_pad; i++) con_write(BOX_H);
        con_write(title);
        for (i = 0; i < right_pad; i++) con_write(BOX_H);
    }
    con_write(BOX_TR);
    
    /* Help text */
    con_cursor_pos(st->top_row + 1, 0);
    con_write(BOX_V);
    {
        char help[256];
        snprintf(help, sizeof(help), " %-3s | %-7s | %s",
                 "Sel", "Version", "Drive");
        con_write_padded(help, inner);
    }
    con_write(BOX_V);
    
    /* Separator line */
    con_cursor_pos(st->top_row + 2, 0);
    con_write(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) con_write(BOX_H);
    }
    con_write(BOX_MR);
    
    /* Drive list */
    for (int row = 0; row < st->visible_rows; row++) {
        int idx = st->scroll_top + row;
        con_cursor_pos(list_start + row, 0);
        con_write(BOX_V);
        
        if (idx < st->count) {
            bool is_selected_row = (idx == st->selected);
            bool is_checked = selected_drives[idx];
            
            if (is_selected_row && g_ansi) con_write(ANSI_REVERSE);
            
            char line[512];
            snprintf(line, sizeof(line), " [%c] | v%-5u | %c:",
                     is_checked ? 'X' : ' ',
                     versions[idx],
                     drives[idx]);
            con_write_padded(line, inner);
            
            if (is_selected_row && g_ansi) con_write(ANSI_RESET);
        } else {
            con_write_padded("", inner);
        }
        con_write(BOX_V);
    }
    
    /* Footer with instructions */
    int footer_row = list_start + st->visible_rows;
    con_cursor_pos(footer_row, 0);
    con_write(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) con_write(BOX_H);
    }
    con_write(BOX_MR);
    
    con_cursor_pos(footer_row + 1, 0);
    con_write(BOX_V);
    {
        char footer[256];
        snprintf(footer, sizeof(footer), " SPACE=Toggle  A=All  N=None  ENTER=Update Selected  ESC=Skip All ");
        int pad = inner - (int)strlen(footer);
        con_write(footer);
        int i;
        for (i = 0; i < pad; i++) con_write(" ");
    }
    con_write(BOX_V);
    
    con_cursor_pos(footer_row + 2, 0);
    con_write(BOX_BL);
    {
        int i;
        for (i = 0; i < inner; i++) con_write(BOX_H);
    }
    con_write(BOX_BR);
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
    
    ui_open_console();
#if NCD_PLATFORM_WINDOWS
    if (g_hout == INVALID_HANDLE_VALUE || g_hin == INVALID_HANDLE_VALUE) {
        ui_close_console();
        /* Default to update all on TUI failure */
        for (int i = 0; i < count; i++) selected[i] = true;
        return UI_UPDATE_ALL;
    }
#else
    if (g_tty_out < 0 || g_tty_in < 0) {
        ui_close_console();
        for (int i = 0; i < count; i++) selected[i] = true;
        return UI_UPDATE_ALL;
    }
#endif
    
    /* Initialize all as selected */
    for (int i = 0; i < count; i++) selected[i] = true;
    
    int cols, rows, cur_row;
    get_console_size(&cols, &rows, &cur_row);
    
    int bw = cols;
    if (bw > max_width) bw = max_width;
    if (bw < 50) bw = 50;
    
    int max_list = rows - 8;
    if (max_list < 3) max_list = 3;
    if (max_list > 20) max_list = 20;
    int visible = count < max_list ? count : max_list;
    
    int total_rows = 5 + visible;
    for (int i = 0; i < total_rows; i++) con_writeln("");
    
    get_console_size(&cols, &rows, &cur_row);
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
    
    con_hide_cursor();
    draw_drive_box(&st, drives, versions, selected);
    
    int result = UI_UPDATE_NONE;
    
    for (;;) {
        int key = read_key();
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
    clear_area(box_top, total_rows, bw);
    con_cursor_pos(box_top, 0);
    con_show_cursor();
    ui_close_console();
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
                            NcdConfig *cfg)
{
    int w = st->box_width;
    int inner = w - 2;
    int list_start = st->top_row + 3;
    
    /* Title bar */
    con_cursor_pos(st->top_row, 0);
    con_write(BOX_TL);
    {
        char title[128];
        snprintf(title, sizeof(title), " NCD Configuration ");
        int title_len = (int)strlen(title);
        int left_pad = (inner - title_len) / 2;
        int right_pad = inner - title_len - left_pad;
        int i;
        for (i = 0; i < left_pad; i++) con_write(BOX_H);
        con_write(title);
        for (i = 0; i < right_pad; i++) con_write(BOX_H);
    }
    con_write(BOX_TR);
    
    /* Help text */
    con_cursor_pos(st->top_row + 1, 0);
    con_write(BOX_V);
    if (st->input_mode) {
        con_write_padded(" Enter/Tab=Confirm  Esc=Cancel ", inner);
    } else {
        con_write_padded(" UP/DOWN=Navigate  SPACE=Toggle  +/- or 0-9=Adjust ", inner);
    }
    con_write(BOX_V);
    
    /* Separator line */
    con_cursor_pos(st->top_row + 2, 0);
    con_write(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) con_write(BOX_H);
    }
    con_write(BOX_MR);
    
    /* Config items */
    for (int row = 0; row < st->visible_rows; row++) {
        int idx = row;
        con_cursor_pos(list_start + row, 0);
        con_write(BOX_V);
        
        if (idx < item_count) {
            bool is_selected_row = (idx == st->selected);
            ConfigItem *item = &items[idx];
            
            if (is_selected_row && g_ansi) con_write(ANSI_REVERSE);
            
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
                    snprintf(line, sizeof(line), " %s: %d ",
                             item->label,
                             *item->int_value);
                }
            }
            con_write_padded(line, inner);
            
            if (is_selected_row && g_ansi) con_write(ANSI_RESET);
        } else {
            con_write_padded("", inner);
        }
        con_write(BOX_V);
    }
    
    /* Footer with instructions */
    int footer_row = list_start + st->visible_rows;
    con_cursor_pos(footer_row, 0);
    con_write(BOX_ML);
    {
        int i;
        for (i = 0; i < inner; i++) con_write(BOX_H);
    }
    con_write(BOX_MR);
    
    con_cursor_pos(footer_row + 1, 0);
    con_write(BOX_V);
    {
        char footer[256];
        if (st->input_mode) {
            snprintf(footer, sizeof(footer), " Type timeout value (10-3600s) ");
        } else {
            snprintf(footer, sizeof(footer), " ENTER=Save  ESC=Cancel ");
        }
        int pad = inner - (int)strlen(footer);
        con_write(footer);
        int i;
        for (i = 0; i < pad; i++) con_write(" ");
    }
    con_write(BOX_V);
    
    con_cursor_pos(footer_row + 2, 0);
    con_write(BOX_BL);
    {
        int i;
        for (i = 0; i < inner; i++) con_write(BOX_H);
    }
    con_write(BOX_BR);
}

bool ui_edit_config(NcdConfig *cfg)
{
    if (!cfg) return false;
    
    /* Initialize defaults if not already set */
    if (cfg->magic != NCD_CFG_MAGIC) {
        db_config_init_defaults(cfg);
    }
    
    ui_open_console();
#if NCD_PLATFORM_WINDOWS
    if (g_hout == INVALID_HANDLE_VALUE || g_hin == INVALID_HANDLE_VALUE) {
        ui_close_console();
        return false;
    }
#else
    if (g_tty_out < 0 || g_tty_in < 0) {
        ui_close_console();
        return false;
    }
#endif
    
    /* Create working copies of values */
    bool show_hidden = cfg->default_show_hidden;
    bool show_system = cfg->default_show_system;
    bool fuzzy_match = cfg->default_fuzzy_match;
    int timeout = cfg->default_timeout;
    if (timeout < 0) timeout = 300;  /* Default to 300 if not set */
    
    /* Define config items */
    ConfigItem items[] = {
        {"Show hidden dirs (/i)", &show_hidden, true, NULL, 0, 0, NULL},
        {"Show system dirs (/s)", &show_system, true, NULL, 0, 0, NULL},
        {"Fuzzy matching (/z)", &fuzzy_match, true, NULL, 0, 0, NULL},
        {"Scan timeout in seconds (/t)", NULL, false, &timeout, 10, 3600, NULL},
    };
    int item_count = sizeof(items) / sizeof(items[0]);
    
    int cols, rows, cur_row;
    get_console_size(&cols, &rows, &cur_row);
    
    int bw = 60;
    if (bw > cols - 4) bw = cols - 4;
    if (bw < 50) bw = 50;
    
    int max_list = rows - 8;
    if (max_list < 3) max_list = 3;
    int visible = item_count < max_list ? item_count : max_list;
    
    int total_rows = 5 + visible;
    for (int i = 0; i < total_rows; i++) con_writeln("");
    
    get_console_size(&cols, &rows, &cur_row);
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
    
    con_hide_cursor();
    draw_config_box(&st, items, item_count, cfg);
    
    bool saved = false;
    
    for (;;) {
        int key = read_key();
        
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
                    /* Handle digit input (0-9) */
                    if (key >= '0' && key <= '9' && st.input_len < 4) {
                        st.input_buf[st.input_len++] = (char)key;
                        st.input_buf[st.input_len] = '\0';
                    }
                    break;
            }
            draw_config_box(&st, items, item_count, cfg);
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
                cfg->has_defaults = true;
                cfg->magic = NCD_CFG_MAGIC;
                cfg->version = NCD_CFG_VERSION;
                saved = true;
                goto config_done;
            case KEY_ESC:
                /* Cancel */
                saved = false;
                goto config_done;
            default:
                /* Enter input mode on digit press for non-bool items */
                if (key >= '0' && key <= '9' && !items[st.selected].is_bool) {
                    st.input_mode = true;
                    st.input_len = 0;
                    st.input_buf[0] = '\0';
                    st.input_orig_value = *items[st.selected].int_value;
                    /* Add the first digit */
                    st.input_buf[st.input_len++] = (char)key;
                    st.input_buf[st.input_len] = '\0';
                }
                break;
        }
        draw_config_box(&st, items, item_count, cfg);
    }
    
config_done:
    /* Get current console size to clear properly */
    get_console_size(&cols, &rows, &cur_row);
    /* Clear total_rows + 1 to include the bottom border row */
    clear_area(box_top, total_rows + 1, cols);
    con_cursor_pos(box_top, 0);
    con_show_cursor();
    ui_close_console();
    return saved;
}
