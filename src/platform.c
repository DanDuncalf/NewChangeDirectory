#define NCD_PLATFORM_IMPL
#include "platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================ platform-specific setup */

#if NCD_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Windows-specific handle wrappers (unchanged) */
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
#include <pthread.h>
#include <time.h>
#include <ctype.h>

/* fd encoding: encode as (fd+1) cast to void* so NULL == invalid */
static inline void   *fd_enc(int fd)           { return (void *)(intptr_t)(fd + 1); }
static inline int     fd_dec(PlatformHandle h) { return (int)(intptr_t)h - 1; }

/* Raw mode state */
static struct termios g_saved_termios;
static int            g_raw_fd = -1;

#else
#error "Unsupported platform"
#endif

/* ================================================================ shared helpers */

char *platform_strtok(char *str, const char *delim, char **saveptr)
{
#if NCD_PLATFORM_WINDOWS
    return strtok_s(str, delim, saveptr);
#else
    return strtok_r(str, delim, saveptr);
#endif
}

bool platform_strncat_s(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0) return false;
    if (!src) return true;
    size_t dst_len = strnlen(dst, dstsz);
    if (dst_len >= dstsz) return false;
    size_t src_len = strlen(src);
    size_t copy = (src_len < dstsz - dst_len - 1) ? src_len : (dstsz - dst_len - 1);
    if (copy > 0) memcpy(dst + dst_len, src, copy);
    dst[dst_len + copy] = '\0';
    return src_len <= copy;
}

bool platform_strncpy_s(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0) return false;
    if (!src) { dst[0] = '\0'; return true; }
#if NCD_PLATFORM_WINDOWS
#ifdef _MSC_VER
    if (strncpy_s(dst, dstsz, src, _TRUNCATE) == 0) {
        return strlen(src) < dstsz;
    }
    return false;
#else
    size_t src_len = strlen(src);
    if (dstsz > 0) {
        size_t copy = (src_len < dstsz - 1) ? src_len : dstsz - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return src_len < dstsz;
#endif
#else
    size_t src_len = strlen(src);
    size_t copy = (src_len < dstsz - 1) ? src_len : (dstsz > 0 ? dstsz - 1 : 0);
    if (copy > 0) memcpy(dst, src, copy);
    dst[copy] = '\0';
    return src_len < dstsz;
#endif
}

/* ================================================================ console I/O */

#if NCD_PLATFORM_WINDOWS

PlatformHandle platform_console_open_out(bool read_write)
{
    DWORD access = GENERIC_READ | GENERIC_WRITE;
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
    const char *p = s;
    char buf[4096];
    int bi = 0;
    DWORD written = 0;
    while (*p) {
        if (bi >= (int)sizeof(buf) - 4) {
            WriteConsoleA((HANDLE)h, buf, (DWORD)bi, &written, NULL);
            bi = 0;
        }
        if (*p == '\n') {
            buf[bi++] = '\r'; buf[bi++] = '\n';
            p++;
        } else {
            buf[bi++] = *p++;
        }
    }
    if (bi > 0) WriteConsoleA((HANDLE)h, buf, (DWORD)bi, &written, NULL);
    return true;
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

#elif NCD_PLATFORM_LINUX

static void linux_set_raw(int fd)
{
    if (g_raw_fd >= 0) return;
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

static void tty_write(int fd, const void *buf, size_t n)
{
    ssize_t r = write(fd, buf, n); (void)r;
}

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
        out->window_left = out->window_top = 0;
        out->window_right = 79; out->window_bottom = 24;
        out->cursor_x = out->cursor_y = 0;
        out->buffer_w = 80; out->buffer_h = 25;
        return false;
    }
    out->window_left   = 0;
    out->window_right  = ws.ws_col - 1;
    out->window_top    = 0;
    out->window_bottom = ws.ws_row - 1;
    out->buffer_w      = ws.ws_col;
    out->buffer_h      = ws.ws_row;

    /* Try ANSI CPR to get cursor position */
    int rfd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (rfd < 0) { out->cursor_x = out->cursor_y = 0; return true; }

    struct termios old_tio, raw_tio;
    bool have_tio = (tcgetattr(rfd, &old_tio) == 0);
    if (have_tio) {
        raw_tio = old_tio;
        raw_tio.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG);
        raw_tio.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
        raw_tio.c_cc[VMIN]  = 1; raw_tio.c_cc[VTIME] = 0;
        tcsetattr(rfd, TCSANOW, &raw_tio);
    }

    ssize_t wn = write(rfd, "\x1b[6n", 4);
    if (wn != 4) {
        if (have_tio) tcsetattr(rfd, TCSANOW, &old_tio);
        close(rfd);
        out->cursor_x = out->cursor_y = 0;
        return true;
    }

    char resp[32] = {0};
    for (int i = 0; i < (int)sizeof(resp) - 1; i++) {
        fd_set rfds; struct timeval tv = {0, 500000};
        FD_ZERO(&rfds); FD_SET(rfd, &rfds);
        if (select(rfd + 1, &rfds, NULL, NULL, &tv) <= 0) break;
        if (read(rfd, &resp[i], 1) <= 0) break;
        if (resp[i] == 'R') break;
    }

    if (have_tio) tcsetattr(rfd, TCSANOW, &old_tio);
    close(rfd);

    int row = 0, col = 0;
    if (sscanf(resp, "\x1b[%d;%dR", &row, &col) < 1)
        sscanf(resp, "[%d;%dR", &row, &col);
    out->cursor_x = (col > 0) ? col - 1 : 0;
    out->cursor_y = (row > 0) ? row - 1 : 0;
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
    return true;
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

bool platform_console_fill_char(PlatformHandle h, int row, int col, int count, char ch)
{
    if (!h || count <= 0) return false;
    int fd = fd_dec(h);
    char pos[32];
    int pn = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", row + 1, col + 1);
    tty_write(fd, pos, (size_t)pn);
    char buf[256];
    while (count > 0) {
        int chunk = count < (int)sizeof(buf) ? count : (int)sizeof(buf);
        memset(buf, ch, (size_t)chunk);
        tty_write(fd, buf, (size_t)chunk);
        count -= chunk;
    }
    return true;
}

bool platform_console_fill_attr_default(PlatformHandle h, int row, int col, int count)
{
    if (!h) return false;
    int fd = fd_dec(h);
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[%d;%dH\x1b[0m", row + 1, col + 1);
    tty_write(fd, seq, (size_t)n);
    (void)count;
    return true;
}

bool platform_console_write_at(PlatformHandle h, int row, int col, const char *s)
{
    if (!h || !s) return false;
    int fd = fd_dec(h);
    char pos[32];
    int pn = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", row + 1, col + 1);
    tty_write(fd, pos, (size_t)pn);
    size_t len = strlen(s);
    return write(fd, s, len) == (ssize_t)len;
}

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
                    case '1': case '7': read_byte_timeout(fd, &s2, 100); *out_vk = PLATFORM_VK_HOME; return true;
                    case '4': case '8': read_byte_timeout(fd, &s2, 100); *out_vk = PLATFORM_VK_END;  return true;
                    case '5': read_byte_timeout(fd, &s2, 100); *out_vk = PLATFORM_VK_PRIOR; return true;
                    case '6': read_byte_timeout(fd, &s2, 100); *out_vk = PLATFORM_VK_NEXT;  return true;
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

#endif

/* ================================================================ environment / filesystem */

bool platform_get_env(const char *name, char *buf, size_t buf_size)
{
    if (!name || !buf || buf_size == 0) return false;
#if NCD_PLATFORM_WINDOWS
    DWORD n = GetEnvironmentVariableA(name, buf, (DWORD)buf_size);
    return n > 0 && n < (DWORD)buf_size;
#else
    const char *v = getenv(name);
    if (!v) return false;
    size_t len = strlen(v);
    if (len >= buf_size) return false;
    memcpy(buf, v, len + 1);
    return true;
#endif
}

bool platform_get_temp_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
#if NCD_PLATFORM_WINDOWS
    DWORD n = GetTempPathA((DWORD)buf_size, buf);
    return n > 0 && n < (DWORD)buf_size;
#else
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    const char *dir = (xdg && xdg[0]) ? xdg : "/tmp";
    size_t len = strlen(dir);
    bool has_sep = (len > 0 && dir[len - 1] == '/');
    int n = snprintf(buf, buf_size, "%s%s", dir, has_sep ? "" : "/");
    return n > 0 && (size_t)n < buf_size;
#endif
}

bool platform_get_current_dir(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
#if NCD_PLATFORM_WINDOWS
    DWORD n = GetCurrentDirectoryA((DWORD)buf_size, buf);
    return n > 0 && n < (DWORD)buf_size;
#else
    return getcwd(buf, buf_size) != NULL;
#endif
}

bool platform_get_module_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
#if NCD_PLATFORM_WINDOWS
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)buf_size);
    return n > 0 && n < (DWORD)buf_size;
#else
    ssize_t n = readlink("/proc/self/exe", buf, buf_size - 1);
    if (n <= 0) return false;
    buf[n] = '\0';
    return true;
#endif
}

bool platform_delete_file(const char *path)
{
    if (!path) return false;
#if NCD_PLATFORM_WINDOWS
    return DeleteFileA(path) != 0;
#else
    return unlink(path) == 0;
#endif
}

bool platform_move_file(const char *src, const char *dst)
{
    if (!src || !dst) return false;
#if NCD_PLATFORM_WINDOWS
    return MoveFileA(src, dst) != 0;
#else
    return rename(src, dst) == 0;
#endif
}

bool platform_create_dir(const char *path)
{
    if (!path) return false;
#if NCD_PLATFORM_WINDOWS
    return CreateDirectoryA(path, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

bool platform_file_exists(const char *path)
{
    if (!path) return false;
#if NCD_PLATFORM_WINDOWS
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool platform_dir_exists(const char *path)
{
    if (!path) return false;
#if NCD_PLATFORM_WINDOWS
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* ================================================================ database paths */

bool platform_db_default_path(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
#if NCD_PLATFORM_WINDOWS
    char local_app[MAX_PATH];
    if (!platform_get_env("LOCALAPPDATA", local_app, sizeof(local_app))) return false;
    int written = snprintf(buf, buf_size, "%s\\%s", local_app, NCD_DB_FILENAME);
    return written > 0 && (size_t)written < buf_size;
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = xdg && xdg[0] ? xdg : getenv("HOME");
    if (!home || !home[0]) return false;
    char base[MAX_PATH];
    if (xdg && xdg[0]) {
        size_t l = strlen(xdg);
        if (l >= sizeof(base)) return false;
        memcpy(base, xdg, l + 1);
    } else {
        int w = snprintf(base, sizeof(base), "%s/.local/share", home);
        if (w <= 0 || (size_t)w >= sizeof(base)) return false;
    }
    char dir[MAX_PATH + 8];
    snprintf(dir, sizeof(dir), "%s/ncd", base);
    platform_create_dir(dir);
    int w = snprintf(buf, buf_size, "%s/ncd/%s", base, NCD_DB_FILENAME);
    return w > 0 && (size_t)w < buf_size;
#endif
}

bool platform_db_drive_path(char letter, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return false;
#if NCD_PLATFORM_WINDOWS
    char local_app[MAX_PATH];
    if (!platform_get_env("LOCALAPPDATA", local_app, sizeof(local_app))) return false;
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\NCD", local_app);
    platform_create_dir(dir);
    int w = snprintf(buf, buf_size, "%s\\NCD\\ncd_%c.database", local_app, toupper((unsigned char)letter));
    return w > 0 && (size_t)w < buf_size;
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = xdg && xdg[0] ? xdg : getenv("HOME");
    if (!home || !home[0]) return false;
    char base[MAX_PATH];
    if (xdg && xdg[0]) {
        size_t l = strlen(xdg);
        if (l >= sizeof(base)) return false;
        memcpy(base, xdg, l + 1);
    } else {
        int w = snprintf(base, sizeof(base), "%s/.local/share", home);
        if (w <= 0 || (size_t)w >= sizeof(base)) return false;
    }
    char dir[MAX_PATH + 8];
    snprintf(dir, sizeof(dir), "%s/ncd", base);
    platform_create_dir(dir);
    int w = snprintf(buf, buf_size, "%s/ncd/ncd_%02x.database", base, (unsigned char)letter);
    return w > 0 && (size_t)w < buf_size;
#endif
}

/* ================================================================ mount/drive info */

UINT platform_get_drive_type(const char *root)
{
    if (!root) return PLATFORM_DRIVE_UNKNOWN;
#if NCD_PLATFORM_WINDOWS
    return GetDriveTypeA(root);
#else
    struct stat st;
    if (stat(root, &st) != 0)   return PLATFORM_DRIVE_NO_ROOT_DIR;
    if (!S_ISDIR(st.st_mode))   return PLATFORM_DRIVE_UNKNOWN;
    return PLATFORM_DRIVE_FIXED;
#endif
}

DWORD platform_get_logical_drives_mask(void)
{
#if NCD_PLATFORM_WINDOWS
    return GetLogicalDrives();
#else
    return 0;
#endif
}

bool platform_get_volume_label(const char *root, char *label, size_t label_size)
{
    if (!label || label_size == 0) return false;
#if NCD_PLATFORM_WINDOWS
    label[0] = '\0';
    return GetVolumeInformationA(root, label, (DWORD)label_size,
                                 NULL, NULL, NULL, NULL, 0) != 0;
#else
    if (!root) { label[0] = '\0'; return false; }
    strncpy(label, root, label_size - 1);
    label[label_size - 1] = '\0';
    return true;
#endif
}

char platform_get_drive_letter(const char *mount)
{
    if (!mount || !mount[0]) return '\0';
#if NCD_PLATFORM_WINDOWS
    char letter = (char)toupper((unsigned char)mount[0]);
    return (letter >= 'A' && letter <= 'Z') ? letter : '\0';
#else
    /* WSL /mnt/<letter> pattern - handles both /mnt/c and /mnt/c (trailing slash) */
    if (mount[0] == '/' && mount[1] == 'm' && mount[2] == 'n' &&
        mount[3] == 't' && mount[4] == '/') {
        char letter = mount[5];
        if (letter >= 'a' && letter <= 'z') {
            /* Check for /mnt/X or /mnt/X/ or /mnt/X/subdir */
            if (mount[6] == '\0' || mount[6] == '/') {
                return (char)toupper((unsigned char)letter);
            }
        }
    }
    /* Non-WSL mounts: return 0 so they're only scanned when explicitly requested */
    return '\0';
#endif
}

int platform_get_mount_type(const char *mount)
{
    if (!mount || !mount[0]) return PLATFORM_DRIVE_UNKNOWN;
#if NCD_PLATFORM_WINDOWS
    char root[4] = { mount[0], ':', '\\', '\0' };
    return (int)GetDriveTypeA(root);
#else
    struct stat st;
    if (stat(mount, &st) != 0) return PLATFORM_DRIVE_UNKNOWN;
    return PLATFORM_DRIVE_FIXED;
#endif
}

int platform_enumerate_mounts(char mount_bufs[][MAX_PATH],
                              const char *mount_ptrs[],
                              size_t buf_size,
                              int max_mounts)
{
    if (!mount_bufs || !mount_ptrs || max_mounts <= 0) return 0;

#if NCD_PLATFORM_WINDOWS
    DWORD mask = GetLogicalDrives();
    int count = 0;

    for (int i = 0; i < 26 && count < max_mounts; i++) {
        if (!(mask & (1u << i))) continue;

        char letter = (char)('A' + i);
        char root[4] = { letter, ':', '\\', '\0' };
        UINT dtype = GetDriveTypeA(root);

        if (dtype == DRIVE_NO_ROOT_DIR || dtype == DRIVE_UNKNOWN || dtype == DRIVE_CDROM)
            continue;

        if (buf_size < 4) continue;
        snprintf(mount_bufs[count], buf_size, "%c:\\", letter);
        mount_ptrs[count] = mount_bufs[count];
        count++;
    }
    return count;

#else
    if (buf_size < 2) return 0;

    static const char *pseudo_fs[] = {
        "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "securityfs",
        "cgroup", "cgroup2", "pstore", "bpf", "autofs", "hugetlbfs",
        "mqueue", "debugfs", "tracefs", "ramfs", "squashfs", "overlay",
        "fusectl", "nsfs", "efivarfs", "configfs", "selinuxfs", "pipefs",
        "sockfs", "rpc_pipefs", "nfsd", "sunrpc", "cpuset", "xenfs",
        "fuse.portal", "fuse.gvfsd-fuse", NULL
    };

    FILE *mf = fopen("/proc/mounts", "r");
    if (!mf) return 0;

    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), mf) && count < max_mounts) {
        char device[256], mountpoint[MAX_PATH], fstype[64];
        if (sscanf(line, "%255s %4095s %63s", device, mountpoint, fstype) != 3)
            continue;

        /* Skip pseudo filesystems */
        bool is_pseudo = false;
        for (int i = 0; pseudo_fs[i]; i++) {
            if (strcmp(fstype, pseudo_fs[i]) == 0) {
                is_pseudo = true;
                break;
            }
        }
        if (is_pseudo) continue;

        if (strncmp(mountpoint, "/proc", 5) == 0) continue;
        if (strncmp(mountpoint, "/sys", 4) == 0) continue;
        if (strncmp(mountpoint, "/dev", 4) == 0) continue;
        if (strncmp(mountpoint, "/run", 4) == 0) continue;
        /* Skip WSL internal mounts to avoid duplicates and unnecessary scanning */
        if (strncmp(mountpoint, "/mnt/wsl", 8) == 0) continue;
        /* Skip root (/). It contains /mnt/X drives and would cause duplicates */
        if (strcmp(mountpoint, "/") == 0) continue;
        /* Skip WSL special mounts */
        if (strcmp(mountpoint, "/init") == 0) continue;
        if (strncmp(mountpoint, "/usr/lib/wsl", 12) == 0) continue;

        size_t mlen = strlen(mountpoint);
        if (mlen + 1 > buf_size) continue;

        snprintf(mount_bufs[count], buf_size, "%s", mountpoint);
        mount_ptrs[count] = mount_bufs[count];
        count++;
    }

    fclose(mf);
    return count;
#endif
}

/* ================================================================ process spawn */

bool platform_spawn_detached(const char *cmd)
{
    if (!cmd) return false;
#if NCD_PLATFORM_WINDOWS
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
#else
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
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
    return true;
#endif
}

/* ================================================================ timing / atomics */

void platform_sleep_ms(unsigned long ms)
{
#if NCD_PLATFORM_WINDOWS
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
#endif
}

unsigned long platform_tick_ms(void)
{
#if NCD_PLATFORM_WINDOWS
    return GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000L));
#endif
}

long platform_atomic_inc(volatile long *v)
{
    if (!v) return 0;
#if NCD_PLATFORM_WINDOWS
    return InterlockedIncrement(v);
#else
    return __sync_add_and_fetch(v, 1);
#endif
}

long platform_atomic_exchange(volatile long *dst, long value)
{
    if (!dst) return 0;
#if NCD_PLATFORM_WINDOWS
    return InterlockedExchange(dst, value);
#else
    return __sync_lock_test_and_set(dst, value);
#endif
}

/* ================================================================ threading */

#if NCD_PLATFORM_WINDOWS

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

#else

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
        unsigned long deadline = platform_tick_ms() + ms;
        while (platform_tick_ms() < deadline) {
            if (pthread_tryjoin_np(*pt, NULL) == 0) break;
            platform_sleep_ms(10);
        }
        pthread_join(*pt, NULL);
    }
    free(pt);
}

#endif

/* ================================================================ directory enumeration */

#if NCD_PLATFORM_WINDOWS

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
    platform_strncpy_s(fd->name, sizeof(fd->name), wfd.cFileName);
    return true;
}

bool platform_find_next(PlatformFindHandle *fh, PlatformFindData *fd)
{
    if (!fh || !fh->h || !fd) return false;
    WIN32_FIND_DATAA wfd;
    if (!FindNextFileA((HANDLE)fh->h, &wfd)) return false;
    fd->attrs = wfd.dwFileAttributes;
    platform_strncpy_s(fd->name, sizeof(fd->name), wfd.cFileName);
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

#else

typedef struct {
    DIR  *dir;
    char  path[MAX_PATH];
} LinuxFindCtx;

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

    char dir[MAX_PATH];
    platform_strncpy_s(dir, sizeof(dir), pattern);
    size_t len = strlen(dir);
    if (len >= 2 && dir[len - 1] == '*' &&
        (dir[len - 2] == '/' || dir[len - 2] == '\\'))
        dir[len - 2] = '\0';
    else if (len >= 1 && dir[len - 1] == '*')
        dir[len - 1] = '\0';

    for (char *p = dir; *p; p++) if (*p == '\\') *p = '/';

    LinuxFindCtx *ctx = (LinuxFindCtx *)malloc(sizeof(LinuxFindCtx));
    if (!ctx) return false;
    platform_strncpy_s(ctx->path, sizeof(ctx->path), dir);
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

        if (ent->d_type == DT_DIR) {
            attrs |= LATTR_DIR;
        } else if (ent->d_type == DT_LNK) {
            attrs |= LATTR_LINK;
        } else if (ent->d_type == DT_UNKNOWN) {
            char full[MAX_PATH + NAME_MAX + 2];
            snprintf(full, sizeof(full), "%s/%s", ctx->path, ent->d_name);
            struct stat st;
            if (lstat(full, &st) == 0) {
                if (S_ISDIR(st.st_mode)) attrs |= LATTR_DIR;
                if (S_ISLNK(st.st_mode)) attrs |= LATTR_LINK;
            }
        }

        if (ent->d_name[0] == '.') attrs |= LATTR_HIDDEN;

        fd->attrs = attrs;
        platform_strncpy_s(fd->name, sizeof(fd->name), ent->d_name);
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

#endif
