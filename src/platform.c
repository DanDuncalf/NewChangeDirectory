#define NCD_PLATFORM_IMPL
#include "platform.h"

#if NCD_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

HANDLE ncdw_CreateFileA(LPCSTR a,DWORD b,DWORD c,LPSECURITY_ATTRIBUTES d,DWORD e,DWORD f,HANDLE g){return CreateFileA(a,b,c,d,e,f,g);}
BOOL ncdw_CloseHandle(HANDLE a){return CloseHandle(a);}
BOOL ncdw_WriteConsoleA(HANDLE a,const VOID*b,DWORD c,LPDWORD d,LPVOID e){return WriteConsoleA(a,b,c,d,e);}
BOOL ncdw_GetConsoleScreenBufferInfo(HANDLE a,PCONSOLE_SCREEN_BUFFER_INFO b){return GetConsoleScreenBufferInfo(a,b);}
BOOL ncdw_SetConsoleCursorPosition(HANDLE a,COORD b){return SetConsoleCursorPosition(a,b);}
BOOL ncdw_GetConsoleMode(HANDLE a,LPDWORD b){return GetConsoleMode(a,b);}
BOOL ncdw_SetConsoleMode(HANDLE a,DWORD b){return SetConsoleMode(a,b);}
BOOL ncdw_SetConsoleCursorInfo(HANDLE a,const CONSOLE_CURSOR_INFO*b){return SetConsoleCursorInfo(a,b);}
BOOL ncdw_FillConsoleOutputCharacterA(HANDLE a,CHAR b,DWORD c,COORD d,LPDWORD e){return FillConsoleOutputCharacterA(a,b,c,d,e);}
BOOL ncdw_FillConsoleOutputAttribute(HANDLE a,WORD b,DWORD c,COORD d,LPDWORD e){return FillConsoleOutputAttribute(a,b,c,d,e);}
BOOL ncdw_WriteConsoleOutputCharacterA(HANDLE a,LPCSTR b,DWORD c,COORD d,LPDWORD e){return WriteConsoleOutputCharacterA(a,b,c,d,e);}
BOOL ncdw_ReadConsoleInputA(HANDLE a,PINPUT_RECORD b,DWORD c,LPDWORD d){return ReadConsoleInputA(a,b,c,d);}
DWORD ncdw_GetEnvironmentVariableA(LPCSTR a,LPSTR b,DWORD c){return GetEnvironmentVariableA(a,b,c);}
DWORD ncdw_GetTempPathA(DWORD a,LPSTR b){return GetTempPathA(a,b);}
DWORD ncdw_GetCurrentDirectoryA(DWORD a,LPSTR b){return GetCurrentDirectoryA(a,b);}
DWORD ncdw_GetModuleFileNameA(HMODULE a,LPSTR b,DWORD c){return GetModuleFileNameA(a,b,c);}
BOOL ncdw_DeleteFileA(LPCSTR a){return DeleteFileA(a);}
BOOL ncdw_MoveFileA(LPCSTR a,LPCSTR b){return MoveFileA(a,b);}
BOOL ncdw_CreateDirectoryA(LPCSTR a,LPSECURITY_ATTRIBUTES b){return CreateDirectoryA(a,b);}
DWORD ncdw_GetFileAttributesA(LPCSTR a){return GetFileAttributesA(a);}
UINT ncdw_GetDriveTypeA(LPCSTR a){return GetDriveTypeA(a);}
DWORD ncdw_GetLogicalDrives(void){return GetLogicalDrives();}
BOOL ncdw_GetVolumeInformationA(LPCSTR a,LPSTR b,DWORD c,LPDWORD d,LPDWORD e,LPDWORD f,LPSTR g,DWORD h){return GetVolumeInformationA(a,b,c,d,e,f,g,h);}
BOOL ncdw_CreateProcessA(LPCSTR a,LPSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD f,LPVOID g,LPCSTR h,LPSTARTUPINFOA i,LPPROCESS_INFORMATION j){return CreateProcessA(a,b,c,d,e,f,g,h,i,j);}
VOID ncdw_Sleep(DWORD a){Sleep(a);}
DWORD ncdw_GetTickCount(void){return GetTickCount();}
LONG ncdw_InterlockedIncrement(volatile LONG *a){return InterlockedIncrement(a);}
LONG ncdw_InterlockedExchange(volatile LONG *a, LONG b){return InterlockedExchange(a,b);}
HANDLE ncdw_CreateThread(LPSECURITY_ATTRIBUTES a,SIZE_T b,LPTHREAD_START_ROUTINE c,LPVOID d,DWORD e,LPDWORD f){return CreateThread(a,b,c,d,e,f);}
DWORD ncdw_WaitForSingleObject(HANDLE a,DWORD b){return WaitForSingleObject(a,b);}
HANDLE ncdw_FindFirstFileExA(LPCSTR a,FINDEX_INFO_LEVELS b,LPVOID c,FINDEX_SEARCH_OPS d,LPVOID e,DWORD f){return FindFirstFileExA(a,b,c,d,e,f);}
BOOL ncdw_FindNextFileA(HANDLE a,LPWIN32_FIND_DATAA b){return FindNextFileA(a,b);}
BOOL ncdw_FindClose(HANDLE a){return FindClose(a);}

PlatformHandle platform_console_open_out(bool read_write)
{
    DWORD access = read_write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
    HANDLE h = CreateFileA("CONOUT$", access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    return (h == INVALID_HANDLE_VALUE) ? NULL : (PlatformHandle)h;
}

PlatformHandle platform_console_open_in(void)
{
    HANDLE h = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    return (h == INVALID_HANDLE_VALUE) ? NULL : (PlatformHandle)h;
}

void platform_handle_close(PlatformHandle h)
{
    if (h) CloseHandle((HANDLE)h);
}

bool platform_console_write(PlatformHandle h, const char *s)
{
    if (!h || !s) return false;
    DWORD w = 0;
    return WriteConsoleA((HANDLE)h, s, (DWORD)strlen(s), &w, NULL) != 0;
}

bool platform_console_get_info(PlatformHandle h, PlatformConsoleInfo *out)
{
    if (!h || !out) return false;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo((HANDLE)h, &csbi)) return false;
    out->window_left   = csbi.srWindow.Left;
    out->window_right  = csbi.srWindow.Right;
    out->window_top    = csbi.srWindow.Top;
    out->window_bottom = csbi.srWindow.Bottom;
    out->cursor_x      = csbi.dwCursorPosition.X;
    out->cursor_y      = csbi.dwCursorPosition.Y;
    out->buffer_w      = csbi.dwSize.X;
    out->buffer_h      = csbi.dwSize.Y;
    return true;
}

bool platform_console_set_cursor(PlatformHandle h, int row, int col)
{
    if (!h) return false;
    COORD c = { (SHORT)col, (SHORT)row };
    return SetConsoleCursorPosition((HANDLE)h, c) != 0;
}

bool platform_console_enable_ansi(PlatformHandle h)
{
    DWORD mode = 0;
    if (!platform_console_get_mode(h, &mode)) return false;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return platform_console_set_mode(h, mode);
}

bool platform_console_set_cursor_visible(PlatformHandle h, bool visible, int size)
{
    if (!h) return false;
    CONSOLE_CURSOR_INFO ci;
    ci.bVisible = visible ? TRUE : FALSE;
    ci.dwSize   = (DWORD)(size > 0 ? size : 1);
    return SetConsoleCursorInfo((HANDLE)h, &ci) != 0;
}

bool platform_console_get_mode(PlatformHandle h, unsigned long *mode)
{
    if (!h || !mode) return false;
    DWORD m = 0;
    if (!GetConsoleMode((HANDLE)h, &m)) return false;
    *mode = m;
    return true;
}

bool platform_console_set_mode(PlatformHandle h, unsigned long mode)
{
    if (!h) return false;
    return SetConsoleMode((HANDLE)h, (DWORD)mode) != 0;
}

bool platform_console_fill_char(PlatformHandle h, int row, int col, int count, char ch)
{
    if (!h) return false;
    COORD pos = { (SHORT)col, (SHORT)row };
    DWORD written = 0;
    return FillConsoleOutputCharacterA((HANDLE)h, ch, (DWORD)count, pos, &written) != 0;
}

bool platform_console_fill_attr_default(PlatformHandle h, int row, int col, int count)
{
    if (!h) return false;
    COORD pos = { (SHORT)col, (SHORT)row };
    DWORD written = 0;
    return FillConsoleOutputAttribute((HANDLE)h,
                                      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
                                      (DWORD)count, pos, &written) != 0;
}

bool platform_console_write_at(PlatformHandle h, int row, int col, const char *s)
{
    if (!h || !s) return false;
    COORD pos = { (SHORT)col, (SHORT)row };
    DWORD written = 0;
    return WriteConsoleOutputCharacterA((HANDLE)h, s, (DWORD)strlen(s), pos, &written) != 0;
}

bool platform_console_read_key_vk(PlatformHandle h, unsigned short *out_vk)
{
    if (!h || !out_vk) return false;
    INPUT_RECORD ir;
    DWORD read = 0;
    for (;;) {
        if (!ReadConsoleInputA((HANDLE)h, &ir, 1, &read)) return false;
        if (ir.EventType != KEY_EVENT) continue;
        if (!ir.Event.KeyEvent.bKeyDown) continue;
        *out_vk = ir.Event.KeyEvent.wVirtualKeyCode;
        return true;
    }
}

bool platform_get_env(const char *name, char *buf, size_t buf_size)
{
    if (!name || !buf || buf_size == 0) return false;
    DWORD n = GetEnvironmentVariableA(name, buf, (DWORD)buf_size);
    return n > 0 && n < (DWORD)buf_size;
}

bool platform_get_temp_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
    DWORD n = GetTempPathA((DWORD)buf_size, buf);
    return n > 0 && n < (DWORD)buf_size;
}

bool platform_get_current_dir(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
    DWORD n = GetCurrentDirectoryA((DWORD)buf_size, buf);
    return n > 0 && n < (DWORD)buf_size;
}

bool platform_get_module_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)buf_size);
    return n > 0 && n < (DWORD)buf_size;
}

bool platform_delete_file(const char *path)
{
    return DeleteFileA(path) != 0;
}

bool platform_move_file(const char *src, const char *dst)
{
    return MoveFileA(src, dst) != 0;
}

bool platform_create_dir(const char *path)
{
    return CreateDirectoryA(path, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool platform_file_exists(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool platform_dir_exists(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

UINT platform_get_drive_type(const char *root)
{
    return GetDriveTypeA(root);
}

DWORD platform_get_logical_drives_mask(void)
{
    return GetLogicalDrives();
}

bool platform_get_volume_label(const char *root, char *label, size_t label_size)
{
    if (!label || label_size == 0) return false;
    label[0] = '\0';
    return GetVolumeInformationA(root, label, (DWORD)label_size,
                                 NULL, NULL, NULL, NULL, 0) != 0;
}

bool platform_spawn_detached(const char *cmd)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    BOOL ok = CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, FALSE,
                             DETACHED_PROCESS | CREATE_NO_WINDOW,
                             NULL, NULL, &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    return ok != 0;
}

void platform_sleep_ms(unsigned long ms)
{
    Sleep((DWORD)ms);
}

unsigned long platform_tick_ms(void)
{
    return GetTickCount();
}

long platform_atomic_inc(volatile long *v)
{
    return InterlockedIncrement(v);
}

long platform_atomic_exchange(volatile long *dst, long value)
{
    return InterlockedExchange(dst, value);
}

typedef struct {
    unsigned long (*fn)(void *);
    void *param;
} ThreadThunk;

static DWORD WINAPI thread_entry(LPVOID p)
{
    ThreadThunk *t = (ThreadThunk *)p;
    unsigned long rc = t->fn(t->param);
    HeapFree(GetProcessHeap(), 0, t);
    return (DWORD)rc;
}

PlatformHandle platform_thread_create(unsigned long (*fn)(void *), void *param)
{
    ThreadThunk *t = (ThreadThunk *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ThreadThunk));
    if (!t) return NULL;
    t->fn = fn;
    t->param = param;
    HANDLE h = CreateThread(NULL, 0, thread_entry, t, 0, NULL);
    if (!h) HeapFree(GetProcessHeap(), 0, t);
    return (PlatformHandle)h;
}

void platform_thread_wait(PlatformHandle h, unsigned long ms)
{
    if (h) WaitForSingleObject((HANDLE)h, (DWORD)ms);
}

bool platform_find_dirs_begin(const char *pattern, PlatformFindHandle *fh, PlatformFindData *fd)
{
    if (!fh || !fd) return false;
    WIN32_FIND_DATAA wfd;
    HANDLE h = FindFirstFileExA(pattern, FindExInfoBasic, &wfd,
                                FindExSearchLimitToDirectories, NULL,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        fh->h = NULL;
        return false;
    }
    fh->h = (PlatformHandle)h;
    fd->attrs = wfd.dwFileAttributes;
    strncpy(fd->name, wfd.cFileName, sizeof(fd->name) - 1);
    fd->name[sizeof(fd->name) - 1] = '\0';
    return true;
}

bool platform_find_next(PlatformFindHandle *fh, PlatformFindData *fd)
{
    if (!fh || !fh->h || !fd) return false;
    WIN32_FIND_DATAA wfd;
    if (!FindNextFileA((HANDLE)fh->h, &wfd)) return false;
    fd->attrs = wfd.dwFileAttributes;
    strncpy(fd->name, wfd.cFileName, sizeof(fd->name) - 1);
    fd->name[sizeof(fd->name) - 1] = '\0';
    return true;
}

void platform_find_close(PlatformFindHandle *fh)
{
    if (!fh || !fh->h) return;
    FindClose((HANDLE)fh->h);
    fh->h = NULL;
}

bool platform_find_is_directory(const PlatformFindData *fd)
{
    return fd && (fd->attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool platform_find_is_hidden(const PlatformFindData *fd)
{
    return fd && (fd->attrs & FILE_ATTRIBUTE_HIDDEN);
}

bool platform_find_is_system(const PlatformFindData *fd)
{
    return fd && (fd->attrs & FILE_ATTRIBUTE_SYSTEM);
}

bool platform_find_is_reparse(const PlatformFindData *fd)
{
    return fd && (fd->attrs & FILE_ATTRIBUTE_REPARSE_POINT);
}

#elif NCD_PLATFORM_LINUX

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

/* ---- fd <-> PlatformHandle encoding ------------------------------------ */
/* Encode fd as (fd+1) cast to void* so NULL == invalid.                   */
static inline void   *fd_enc(int fd)           { return (void *)(intptr_t)(fd + 1); }
static inline int     fd_dec(PlatformHandle h) { return (int)(intptr_t)h - 1; }

/* ---- raw-mode tracking ------------------------------------------------- */
static struct termios g_saved_termios;
static int            g_raw_fd = -1;

static void linux_set_raw(int fd)
{
    if (g_raw_fd >= 0) return;   /* already in raw mode */
    tcgetattr(fd, &g_saved_termios);
    struct termios raw = g_saved_termios;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &raw);
    g_raw_fd = fd;
}

static void linux_restore_raw(int fd)
{
    if (g_raw_fd == fd) {
        tcsetattr(fd, TCSAFLUSH, &g_saved_termios);
        g_raw_fd = -1;
    }
}

/* ---- console ----------------------------------------------------------- */

PlatformHandle platform_console_open_out(bool read_write)
{
    int flags = read_write ? O_RDWR : O_WRONLY;
    int fd = open("/dev/tty", flags | O_NOCTTY);
    return fd_enc(fd);
}

PlatformHandle platform_console_open_in(void)
{
    int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (fd >= 0) linux_set_raw(fd);
    return fd_enc(fd);
}

void platform_handle_close(PlatformHandle h)
{
    if (!h) return;
    int fd = fd_dec(h);
    linux_restore_raw(fd);
    close(fd);
}

bool platform_console_write(PlatformHandle h, const char *s)
{
    if (!h || !s) return false;
    int fd = fd_dec(h);
    size_t len = strlen(s);
    return write(fd, s, len) == (ssize_t)len;
}

bool platform_console_get_info(PlatformHandle h, PlatformConsoleInfo *out)
{
    if (!h || !out) return false;
    int fd = fd_dec(h);
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) {
        out->window_left   = 0;
        out->window_right  = 79;
        out->window_top    = 0;
        out->window_bottom = 24;
        out->cursor_x      = 0;
        out->cursor_y      = 0;
        out->buffer_w      = 80;
        out->buffer_h      = 25;
        return false;
    }
    out->window_left   = 0;
    out->window_right  = ws.ws_col - 1;
    out->window_top    = 0;
    out->window_bottom = ws.ws_row - 1;
    out->cursor_x      = 0;
    out->cursor_y      = 0;
    out->buffer_w      = ws.ws_col;
    out->buffer_h      = ws.ws_row;
    return true;
}

bool platform_console_set_cursor(PlatformHandle h, int row, int col)
{
    if (!h) return false;
    int fd = fd_dec(h);
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row + 1, col + 1);
    return write(fd, buf, (size_t)n) == n;
}

bool platform_console_enable_ansi(PlatformHandle h)
{
    (void)h;
    return true;   /* ANSI always available on Linux terminals */
}

bool platform_console_set_cursor_visible(PlatformHandle h, bool visible, int size)
{
    (void)size;
    if (!h) return false;
    int fd = fd_dec(h);
    const char *seq = visible ? "\x1b[?25h" : "\x1b[?25l";
    ssize_t n = write(fd, seq, strlen(seq));
    return n > 0;
}

bool platform_console_get_mode(PlatformHandle h, unsigned long *mode)
{
    (void)h;
    if (mode) *mode = 0;
    return true;
}

bool platform_console_set_mode(PlatformHandle h, unsigned long mode)
{
    (void)h; (void)mode;
    return true;
}

/* write() wrapper that discards the return value without -Wunused-result */
static void tty_write_p(int fd, const void *buf, size_t n)
{
    ssize_t r = write(fd, buf, n); (void)r;
}

bool platform_console_fill_char(PlatformHandle h, int row, int col, int count, char ch)
{
    if (!h || count <= 0) return false;
    int fd = fd_dec(h);
    char pos[32];
    int pn = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", row + 1, col + 1);
    tty_write_p(fd, pos, (size_t)pn);
    char buf[256];
    while (count > 0) {
        int chunk = count < (int)sizeof(buf) ? count : (int)sizeof(buf);
        memset(buf, ch, (size_t)chunk);
        tty_write_p(fd, buf, (size_t)chunk);
        count -= chunk;
    }
    return true;
}

bool platform_console_fill_attr_default(PlatformHandle h, int row, int col, int count)
{
    /* On Linux just reset colour attributes; we don't re-paint the chars. */
    if (!h) return false;
    int fd = fd_dec(h);
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[%d;%dH\x1b[0m", row + 1, col + 1);
    tty_write_p(fd, seq, (size_t)n);
    (void)count;
    return true;
}

bool platform_console_write_at(PlatformHandle h, int row, int col, const char *s)
{
    if (!h || !s) return false;
    int fd = fd_dec(h);
    char pos[32];
    int pn = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", row + 1, col + 1);
    tty_write_p(fd, pos, (size_t)pn);
    size_t len = strlen(s);
    return write(fd, s, len) == (ssize_t)len;
}

/* ---- key input --------------------------------------------------------- */

static bool read_byte_timeout(int fd, unsigned char *out, int ms)
{
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000L;
    if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) return false;
    return read(fd, out, 1) == 1;
}

bool platform_console_read_key_vk(PlatformHandle h, unsigned short *out_vk)
{
    if (!h || !out_vk) return false;
    int fd = fd_dec(h);

    for (;;) {
        unsigned char c;
        if (read(fd, &c, 1) != 1) return false;

        if (c == 0x1b) {
            unsigned char s0 = 0, s1 = 0, s2 = 0;
            if (!read_byte_timeout(fd, &s0, 100)) {
                *out_vk = PLATFORM_VK_ESCAPE; return true;
            }
            if (s0 == '[') {
                if (!read_byte_timeout(fd, &s1, 100)) continue;
                switch (s1) {
                    case 'A': *out_vk = PLATFORM_VK_UP;    return true;
                    case 'B': *out_vk = PLATFORM_VK_DOWN;  return true;
                    case 'C': *out_vk = PLATFORM_VK_RIGHT; return true;
                    case 'D': *out_vk = PLATFORM_VK_LEFT;  return true;
                    case 'H': *out_vk = PLATFORM_VK_HOME;  return true;
                    case 'F': *out_vk = PLATFORM_VK_END;   return true;
                    case '1': case '7':
                        read_byte_timeout(fd, &s2, 100);   /* consume ~ */
                        *out_vk = PLATFORM_VK_HOME; return true;
                    case '4': case '8':
                        read_byte_timeout(fd, &s2, 100);
                        *out_vk = PLATFORM_VK_END;  return true;
                    case '5':
                        read_byte_timeout(fd, &s2, 100);
                        *out_vk = PLATFORM_VK_PRIOR; return true;
                    case '6':
                        read_byte_timeout(fd, &s2, 100);
                        *out_vk = PLATFORM_VK_NEXT;  return true;
                    default: continue;
                }
            } else if (s0 == 'O') {
                if (!read_byte_timeout(fd, &s1, 100)) continue;
                switch (s1) {
                    case 'A': *out_vk = PLATFORM_VK_UP;    return true;
                    case 'B': *out_vk = PLATFORM_VK_DOWN;  return true;
                    case 'C': *out_vk = PLATFORM_VK_RIGHT; return true;
                    case 'D': *out_vk = PLATFORM_VK_LEFT;  return true;
                    case 'H': *out_vk = PLATFORM_VK_HOME;  return true;
                    case 'F': *out_vk = PLATFORM_VK_END;   return true;
                    default: continue;
                }
            }
            *out_vk = PLATFORM_VK_ESCAPE;
            return true;
        }

        switch (c) {
            case '\r': case '\n': *out_vk = PLATFORM_VK_RETURN; return true;
            case 0x7f: case '\b': *out_vk = PLATFORM_VK_BACK;   return true;
            case '\t':             *out_vk = PLATFORM_VK_TAB;    return true;
            case 'q':  case 'Q':  *out_vk = PLATFORM_VK_ESCAPE; return true;
            default:   continue;
        }
    }
}

/* ---- environment / paths ----------------------------------------------- */

bool platform_get_env(const char *name, char *buf, size_t buf_size)
{
    if (!name || !buf || buf_size == 0) return false;
    const char *v = getenv(name);
    if (!v) return false;
    size_t len = strlen(v);
    if (len >= buf_size) return false;
    memcpy(buf, v, len + 1);
    return true;
}

bool platform_get_temp_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    const char *dir = (xdg && xdg[0]) ? xdg : "/tmp";
    size_t len = strlen(dir);
    bool has_sep = (len > 0 && dir[len - 1] == '/');
    int n = snprintf(buf, buf_size, "%s%s", dir, has_sep ? "" : "/");
    return n > 0 && (size_t)n < buf_size;
}

bool platform_get_current_dir(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
    return getcwd(buf, buf_size) != NULL;
}

bool platform_get_module_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
    ssize_t n = readlink("/proc/self/exe", buf, buf_size - 1);
    if (n <= 0) return false;
    buf[n] = '\0';
    return true;
}

/* ---- file operations --------------------------------------------------- */

bool platform_delete_file(const char *path)
{
    return unlink(path) == 0;
}

bool platform_move_file(const char *src, const char *dst)
{
    return rename(src, dst) == 0;
}

bool platform_create_dir(const char *path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool platform_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool platform_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---- drive / mount operations ------------------------------------------ */

UINT platform_get_drive_type(const char *root)
{
    /* On Linux just check if the path is a real directory */
    if (!root) return PLATFORM_DRIVE_UNKNOWN;
    struct stat st;
    if (stat(root, &st) != 0)   return PLATFORM_DRIVE_NO_ROOT_DIR;
    if (!S_ISDIR(st.st_mode))   return PLATFORM_DRIVE_UNKNOWN;
    return PLATFORM_DRIVE_FIXED;
}

DWORD platform_get_logical_drives_mask(void)
{
    /* Not meaningful on Linux; callers use the scanner directly */
    return 0;
}

bool platform_get_volume_label(const char *root, char *label, size_t label_size)
{
    if (!label || label_size == 0) return false;
    /* Use the mount point path itself as the "label" on Linux */
    if (!root) { label[0] = '\0'; return false; }
    strncpy(label, root, label_size - 1);
    label[label_size - 1] = '\0';
    return true;
}

/* ---- process spawn ----------------------------------------------------- */

bool platform_spawn_detached(const char *cmd)
{
    if (!cmd) return false;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        /* child: detach from terminal and exec */
        setsid();
        /* Close stdin/out/err */
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, 0);
            dup2(null_fd, 1);
            dup2(null_fd, 2);
            close(null_fd);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    /* parent: don't wait */
    return true;
}

/* ---- timing ------------------------------------------------------------ */

void platform_sleep_ms(unsigned long ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
}

unsigned long platform_tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000L));
}

/* ---- atomics ----------------------------------------------------------- */

long platform_atomic_inc(volatile long *v)
{
    return __sync_add_and_fetch(v, 1);
}

long platform_atomic_exchange(volatile long *dst, long value)
{
    return __sync_lock_test_and_set(dst, value);
}

/* ---- threading --------------------------------------------------------- */

typedef struct {
    unsigned long (*fn)(void *);
    void *param;
} LinuxThunk;

static void *linux_thread_entry(void *p)
{
    LinuxThunk *t = (LinuxThunk *)p;
    unsigned long rc = t->fn(t->param);
    free(t);
    return (void *)(uintptr_t)rc;
}

PlatformHandle platform_thread_create(unsigned long (*fn)(void *), void *param)
{
    LinuxThunk *t = (LinuxThunk *)malloc(sizeof(LinuxThunk));
    if (!t) return NULL;
    t->fn    = fn;
    t->param = param;

    pthread_t *pt = (pthread_t *)malloc(sizeof(pthread_t));
    if (!pt) { free(t); return NULL; }

    if (pthread_create(pt, NULL, linux_thread_entry, t) != 0) {
        free(t); free(pt); return NULL;
    }
    return (PlatformHandle)pt;
}

void platform_thread_wait(PlatformHandle h, unsigned long ms)
{
    if (!h) return;
    pthread_t *pt = (pthread_t *)h;
    if (ms == 0 || ms == 0xFFFFFFFFUL) {
        pthread_join(*pt, NULL);
    } else {
        /* Timed join via polling */
        unsigned long deadline = platform_tick_ms() + ms;
        while (platform_tick_ms() < deadline) {
            if (pthread_tryjoin_np(*pt, NULL) == 0) break;
            platform_sleep_ms(10);
        }
        pthread_join(*pt, NULL);   /* final join to clean up */
    }
    free(pt);
}

/* ---- directory find ---------------------------------------------------- */

/*
 * On Linux we allocate a small context on the heap so that the caller's
 * PlatformFindHandle.h (a void*) can carry both the DIR* and the directory
 * path needed for lstat when d_type == DT_UNKNOWN.
 */
typedef struct {
    DIR  *dir;
    char  path[MAX_PATH];
} LinuxFindCtx;

/* Linux attribute bits stored in PlatformFindData.attrs */
#define LATTR_DIR    (1UL << 0)
#define LATTR_HIDDEN (1UL << 1)
#define LATTR_SYSTEM (1UL << 2)
#define LATTR_LINK   (1UL << 3)

bool platform_find_dirs_begin(const char *pattern,
                               PlatformFindHandle *fh,
                               PlatformFindData   *fd)
{
    if (!fh || !fd) return false;
    fh->h = NULL;

    /* Strip trailing backslash-star or slash-star from the pattern */
    char dir[MAX_PATH];
    strncpy(dir, pattern, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    size_t len = strlen(dir);
    if (len >= 2 && dir[len - 1] == '*' &&
        (dir[len - 2] == '/' || dir[len - 2] == '\\'))
        dir[len - 2] = '\0';
    else if (len >= 1 && dir[len - 1] == '*')
        dir[len - 1] = '\0';

    /* Replace any remaining backslashes with forward slashes */
    for (char *p = dir; *p; p++) if (*p == '\\') *p = '/';

    LinuxFindCtx *ctx = (LinuxFindCtx *)malloc(sizeof(LinuxFindCtx));
    if (!ctx) return false;
    strncpy(ctx->path, dir, sizeof(ctx->path) - 1);
    ctx->path[sizeof(ctx->path) - 1] = '\0';
    ctx->dir = opendir(dir);
    if (!ctx->dir) { free(ctx); return false; }

    fh->h = (PlatformHandle)ctx;
    return platform_find_next(fh, fd);
}

bool platform_find_next(PlatformFindHandle *fh, PlatformFindData *fd)
{
    if (!fh || !fh->h || !fd) return false;
    LinuxFindCtx *ctx = (LinuxFindCtx *)fh->h;

    struct dirent *ent;
    while ((ent = readdir(ctx->dir)) != NULL) {
        unsigned long attrs = 0;

        /* Determine type */
        if (ent->d_type == DT_DIR) {
            attrs |= LATTR_DIR;
        } else if (ent->d_type == DT_LNK) {
            attrs |= LATTR_LINK;
        } else if (ent->d_type == DT_UNKNOWN) {
            /* Fall back to lstat for filesystems that don't fill d_type */
            char full[MAX_PATH + NAME_MAX + 2];
            snprintf(full, sizeof(full), "%s/%s", ctx->path, ent->d_name);
            struct stat st;
            if (lstat(full, &st) == 0) {
                if (S_ISDIR(st.st_mode)) attrs |= LATTR_DIR;
                if (S_ISLNK(st.st_mode)) attrs |= LATTR_LINK;
            }
        } else {
            /* Regular file or other — not a directory */
        }

        if (ent->d_name[0] == '.') attrs |= LATTR_HIDDEN;

        fd->attrs = attrs;
        strncpy(fd->name, ent->d_name, sizeof(fd->name) - 1);
        fd->name[sizeof(fd->name) - 1] = '\0';
        return true;
    }
    return false;
}

void platform_find_close(PlatformFindHandle *fh)
{
    if (!fh || !fh->h) return;
    LinuxFindCtx *ctx = (LinuxFindCtx *)fh->h;
    if (ctx->dir) closedir(ctx->dir);
    free(ctx);
    fh->h = NULL;
}

bool platform_find_is_directory(const PlatformFindData *fd)
{
    return fd && (fd->attrs & LATTR_DIR);
}

bool platform_find_is_hidden(const PlatformFindData *fd)
{
    return fd && (fd->attrs & LATTR_HIDDEN);
}

bool platform_find_is_system(const PlatformFindData *fd)
{
    return fd && (fd->attrs & LATTR_SYSTEM);
}

bool platform_find_is_reparse(const PlatformFindData *fd)
{
    return fd && (fd->attrs & LATTR_LINK);
}

#else
#error Unsupported platform in platform.c
#endif
