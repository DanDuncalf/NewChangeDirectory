#ifndef NCD_PLATFORM_H
#define NCD_PLATFORM_H

#include "ncd.h"

#ifdef __cplusplus
extern "C" {
#endif

#if NCD_PLATFORM_WINDOWS && !defined(NCD_PLATFORM_IMPL)
/* Compatibility wrapper layer: remap direct Win32 calls through platform.c */
HANDLE ncdw_CreateFileA(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
BOOL ncdw_CloseHandle(HANDLE);
BOOL ncdw_WriteConsoleA(HANDLE,const VOID*,DWORD,LPDWORD,LPVOID);
BOOL ncdw_GetConsoleScreenBufferInfo(HANDLE,PCONSOLE_SCREEN_BUFFER_INFO);
BOOL ncdw_SetConsoleCursorPosition(HANDLE,COORD);
BOOL ncdw_GetConsoleMode(HANDLE,LPDWORD);
BOOL ncdw_SetConsoleMode(HANDLE,DWORD);
BOOL ncdw_SetConsoleCursorInfo(HANDLE,const CONSOLE_CURSOR_INFO*);
BOOL ncdw_FillConsoleOutputCharacterA(HANDLE,CHAR,DWORD,COORD,LPDWORD);
BOOL ncdw_FillConsoleOutputAttribute(HANDLE,WORD,DWORD,COORD,LPDWORD);
BOOL ncdw_WriteConsoleOutputCharacterA(HANDLE,LPCSTR,DWORD,COORD,LPDWORD);
BOOL ncdw_ReadConsoleInputA(HANDLE,PINPUT_RECORD,DWORD,LPDWORD);
DWORD ncdw_GetEnvironmentVariableA(LPCSTR,LPSTR,DWORD);
DWORD ncdw_GetTempPathA(DWORD,LPSTR);
DWORD ncdw_GetCurrentDirectoryA(DWORD,LPSTR);
DWORD ncdw_GetModuleFileNameA(HMODULE,LPSTR,DWORD);
BOOL ncdw_DeleteFileA(LPCSTR);
BOOL ncdw_MoveFileA(LPCSTR,LPCSTR);
BOOL ncdw_CreateDirectoryA(LPCSTR,LPSECURITY_ATTRIBUTES);
DWORD ncdw_GetFileAttributesA(LPCSTR);
UINT ncdw_GetDriveTypeA(LPCSTR);
DWORD ncdw_GetLogicalDrives(void);
BOOL ncdw_GetVolumeInformationA(LPCSTR,LPSTR,DWORD,LPDWORD,LPDWORD,LPDWORD,LPSTR,DWORD);
BOOL ncdw_CreateProcessA(LPCSTR,LPSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCSTR,LPSTARTUPINFOA,LPPROCESS_INFORMATION);
VOID ncdw_Sleep(DWORD);
DWORD ncdw_GetTickCount(void);
LONG ncdw_InterlockedIncrement(volatile LONG *);
LONG ncdw_InterlockedExchange(volatile LONG *, LONG);
HANDLE ncdw_CreateThread(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD);
DWORD ncdw_WaitForSingleObject(HANDLE,DWORD);
HANDLE ncdw_FindFirstFileExA(LPCSTR,FINDEX_INFO_LEVELS,LPVOID,FINDEX_SEARCH_OPS,LPVOID,DWORD);
BOOL ncdw_FindNextFileA(HANDLE,LPWIN32_FIND_DATAA);
BOOL ncdw_FindClose(HANDLE);

#define CreateFileA ncdw_CreateFileA
#define CloseHandle ncdw_CloseHandle
#define WriteConsoleA ncdw_WriteConsoleA
#define GetConsoleScreenBufferInfo ncdw_GetConsoleScreenBufferInfo
#define SetConsoleCursorPosition ncdw_SetConsoleCursorPosition
#define GetConsoleMode ncdw_GetConsoleMode
#define SetConsoleMode ncdw_SetConsoleMode
#define SetConsoleCursorInfo ncdw_SetConsoleCursorInfo
#define FillConsoleOutputCharacterA ncdw_FillConsoleOutputCharacterA
#define FillConsoleOutputAttribute ncdw_FillConsoleOutputAttribute
#define WriteConsoleOutputCharacterA ncdw_WriteConsoleOutputCharacterA
#define ReadConsoleInputA ncdw_ReadConsoleInputA
#define GetEnvironmentVariableA ncdw_GetEnvironmentVariableA
#define GetTempPathA ncdw_GetTempPathA
#define GetCurrentDirectoryA ncdw_GetCurrentDirectoryA
#define GetModuleFileNameA ncdw_GetModuleFileNameA
#define DeleteFileA ncdw_DeleteFileA
#define MoveFileA ncdw_MoveFileA
#define CreateDirectoryA ncdw_CreateDirectoryA
#define GetFileAttributesA ncdw_GetFileAttributesA
#define GetDriveTypeA ncdw_GetDriveTypeA
#define GetLogicalDrives ncdw_GetLogicalDrives
#define GetVolumeInformationA ncdw_GetVolumeInformationA
#define CreateProcessA ncdw_CreateProcessA
#define Sleep ncdw_Sleep
#define GetTickCount ncdw_GetTickCount
#ifdef InterlockedIncrement
#undef InterlockedIncrement
#endif
#define InterlockedIncrement ncdw_InterlockedIncrement
#ifdef InterlockedExchange
#undef InterlockedExchange
#endif
#define InterlockedExchange ncdw_InterlockedExchange
#define CreateThread ncdw_CreateThread
#define WaitForSingleObject ncdw_WaitForSingleObject
#define FindFirstFileExA ncdw_FindFirstFileExA
#define FindNextFileA ncdw_FindNextFileA
#define FindClose ncdw_FindClose
#endif

typedef void *PlatformHandle;

typedef struct {
    int window_left;
    int window_right;
    int window_top;
    int window_bottom;
    int cursor_x;
    int cursor_y;
    int buffer_w;
    int buffer_h;
} PlatformConsoleInfo;

typedef struct {
    PlatformHandle h;
} PlatformFindHandle;

typedef struct {
    unsigned long attrs;
    char          name[MAX_PATH];
} PlatformFindData;

enum {
    PLATFORM_DRIVE_UNKNOWN     = 0,
    PLATFORM_DRIVE_NO_ROOT_DIR = 1,
    PLATFORM_DRIVE_REMOVABLE   = 2,
    PLATFORM_DRIVE_FIXED       = 3,
    PLATFORM_DRIVE_REMOTE      = 4,
    PLATFORM_DRIVE_CDROM       = 5,
    PLATFORM_DRIVE_RAMDISK     = 6
};

enum {
    PLATFORM_VK_UP    = 0x26,
    PLATFORM_VK_DOWN  = 0x28,
    PLATFORM_VK_LEFT  = 0x25,
    PLATFORM_VK_RIGHT = 0x27,
    PLATFORM_VK_PRIOR = 0x21,
    PLATFORM_VK_NEXT  = 0x22,
    PLATFORM_VK_HOME  = 0x24,
    PLATFORM_VK_END   = 0x23,
    PLATFORM_VK_RETURN = 0x0D,
    PLATFORM_VK_ESCAPE = 0x1B,
    PLATFORM_VK_TAB    = 0x09,
    PLATFORM_VK_BACK   = 0x08
};

PlatformHandle platform_console_open_out(bool read_write);
PlatformHandle platform_console_open_in(void);
void platform_handle_close(PlatformHandle h);
bool platform_console_write(PlatformHandle h, const char *s);
bool platform_console_get_info(PlatformHandle h, PlatformConsoleInfo *out);
bool platform_console_set_cursor(PlatformHandle h, int row, int col);
bool platform_console_enable_ansi(PlatformHandle h);
bool platform_console_set_cursor_visible(PlatformHandle h, bool visible, int size);
bool platform_console_get_mode(PlatformHandle h, unsigned long *mode);
bool platform_console_set_mode(PlatformHandle h, unsigned long mode);
bool platform_console_fill_char(PlatformHandle h, int row, int col, int count, char ch);
bool platform_console_fill_attr_default(PlatformHandle h, int row, int col, int count);
bool platform_console_write_at(PlatformHandle h, int row, int col, const char *s);

bool platform_console_read_key_vk(PlatformHandle h, unsigned short *out_vk);

bool platform_get_env(const char *name, char *buf, size_t buf_size);
bool platform_get_temp_path(char *buf, size_t buf_size);
bool platform_get_current_dir(char *buf, size_t buf_size);
bool platform_get_module_path(char *buf, size_t buf_size);

bool platform_delete_file(const char *path);
bool platform_move_file(const char *src, const char *dst);
bool platform_create_dir(const char *path);
bool platform_file_exists(const char *path);
bool platform_dir_exists(const char *path);

UINT platform_get_drive_type(const char *root);
DWORD platform_get_logical_drives_mask(void);
bool platform_get_volume_label(const char *root, char *label, size_t label_size);

bool platform_spawn_detached(const char *cmd);

void platform_sleep_ms(unsigned long ms);
unsigned long platform_tick_ms(void);
long platform_atomic_inc(volatile long *v);
long platform_atomic_exchange(volatile long *dst, long value);

PlatformHandle platform_thread_create(unsigned long (*fn)(void *), void *param);
void platform_thread_wait(PlatformHandle h, unsigned long ms);

bool platform_find_dirs_begin(const char *pattern, PlatformFindHandle *fh, PlatformFindData *fd);
bool platform_find_next(PlatformFindHandle *fh, PlatformFindData *fd);
void platform_find_close(PlatformFindHandle *fh);
bool platform_find_is_directory(const PlatformFindData *fd);
bool platform_find_is_hidden(const PlatformFindData *fd);
bool platform_find_is_system(const PlatformFindData *fd);
bool platform_find_is_reparse(const PlatformFindData *fd);

#ifdef __cplusplus
}
#endif

#endif
