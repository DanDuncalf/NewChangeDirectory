/*
 * platform.c -- NCD-specific extensions to the shared platform library
 */

#include "platform.h"
#include "ncd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#if NCD_PLATFORM_WINDOWS
#include <shlwapi.h>  /* For StrStrIA (case-insensitive substring search) */
#elif NCD_PLATFORM_LINUX
#include <errno.h>
#include <strings.h>  /* For strcasestr on Linux */
#endif

/* NCD-specific pseudo filesystems list */
static const char *pseudo_filesystems[] = {
    "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "securityfs",
    "cgroup", "cgroup2", "pstore", "bpf", "autofs", "hugetlbfs",
    "mqueue", "debugfs", "tracefs", "ramfs", "squashfs", "overlay",
    "fusectl", "nsfs", "efivarfs", "configfs", "selinuxfs", "pipefs",
    "sockfs", "rpc_pipefs", "nfsd", "sunrpc", "cpuset", "xenfs",
    "fuse.portal", "fuse.gvfsd-fuse", NULL
};

/* Check if a filesystem type is a pseudo filesystem */
bool platform_is_pseudo_fs(const char *fstype)
{
    for (int i = 0; pseudo_filesystems[i]; i++) {
        if (strcmp(fstype, pseudo_filesystems[i]) == 0)
            return true;
    }
    return false;
}

/* ================================================================ NCD-specific database paths */

bool ncd_platform_db_default_path(char *buf, size_t buf_size)
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

bool ncd_platform_db_drive_path(char letter, char *buf, size_t buf_size)
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

/* ================================================================ NCD-specific mount enumeration */

int ncd_platform_enumerate_mounts(char mount_bufs[][MAX_PATH],
                                  const char *mount_ptrs[],
                                  size_t buf_size,
                                  int max_mounts)
{
    if (!mount_bufs || !mount_ptrs || max_mounts <= 0) return 0;

#if NCD_PLATFORM_WINDOWS
    /* On Windows, use the standard platform enumeration */
    return platform_enumerate_mounts(mount_bufs, mount_ptrs, buf_size, max_mounts);
#else
    if (buf_size < 2) return 0;

    FILE *mf = fopen("/proc/mounts", "r");
    if (!mf) return 0;

    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), mf) && count < max_mounts) {
        char device[256], mountpoint[MAX_PATH], fstype[64];
        if (sscanf(line, "%255s %4095s %63s", device, mountpoint, fstype) != 3)
            continue;

        /* Skip pseudo filesystems */
        if (platform_is_pseudo_fs(fstype)) continue;

        if (strncmp(mountpoint, "/proc", 5) == 0) continue;
        if (strncmp(mountpoint, "/sys", 4) == 0) continue;
        if (strncmp(mountpoint, "/dev", 4) == 0) continue;
        if (strncmp(mountpoint, "/run", 4) == 0) continue;
        /* Skip WSL internal mounts to avoid duplicates and unnecessary scanning */
        if (strncmp(mountpoint, "/mnt/wsl", 8) == 0) continue;
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

/* ================================================================ NCD-specific platform abstractions */

const char *platform_get_app_title(void)
{
#if NCD_PLATFORM_WINDOWS
    return "NewChangeDirectory (NCD) -- Nifty CD for Windows 64-bit";
#else
    return "NewChangeDirectory (NCD) -- Nifty CD for Linux 64-bit";
#endif
}

int platform_write_help_suffix(char *buf, size_t buf_size)
{
#if NCD_PLATFORM_LINUX
    return snprintf(buf, buf_size,
        "\r\n"
        "Linux/WSL Specific:\r\n"
        "  /r /        Rescan root filesystem only (excludes /mnt/*)\r\n"
        "  X: or X:\\   Navigate Windows drive X: via /mnt/X\r\n");
#else
    (void)buf;
    (void)buf_size;
    return 0;
#endif
}

char platform_parse_drive_from_search(const char *search, char *out_search, size_t out_size)
{
    if (!search || !out_search || out_size == 0) return 0;
    
#if NCD_PLATFORM_WINDOWS
    if (isalpha((unsigned char)search[0]) && search[1] == ':') {
        char drive = (char)toupper((unsigned char)search[0]);
        platform_strncpy_s(out_search, out_size, search + 2);
        return drive;
    } else {
        char cwd[MAX_PATH] = {0};
        platform_get_current_dir(cwd, sizeof(cwd));
        char drive = (char)toupper((unsigned char)cwd[0]);
        platform_strncpy_s(out_search, out_size, search);
        return drive;
    }
#else
    /* On Linux/WSL: if search starts with "X:" treat X as Windows drive letter */
    if (isalpha((unsigned char)search[0]) && search[1] == ':') {
        char drive = (char)toupper((unsigned char)search[0]);
        platform_strncpy_s(out_search, out_size, search + 2);
        return drive;
    } else {
        /* Default to first available mount */
        return '\x01';
    }
#endif
}

bool platform_build_mount_path(char letter, char *out_path, size_t out_size)
{
    if (!out_path || out_size == 0) return false;
    
#if NCD_PLATFORM_WINDOWS
    if (!isalpha((unsigned char)letter)) return false;
    snprintf(out_path, out_size, "%c:\\", (char)toupper((unsigned char)letter));
    return true;
#else
    /* On Linux, find the mount point for this letter */
    char mnt_bufs[26][MAX_PATH];
    const char *mnt_ptrs[26];
    int count = ncd_platform_enumerate_mounts(mnt_bufs, mnt_ptrs, MAX_PATH, 26);
    
    for (int i = 0; i < count; i++) {
        char mnt_letter = platform_get_drive_letter(mnt_ptrs[i]);
        if (mnt_letter == letter) {
            snprintf(out_path, out_size, "%s", mnt_ptrs[i]);
            return true;
        }
    }
    return false;
#endif
}

bool platform_is_drive_specifier(const char *str)
{
    if (!str || !str[0]) return false;
    
#if NCD_PLATFORM_WINDOWS
    return isalpha((unsigned char)str[0]) && str[1] == ':' &&
           (str[2] == '\0' || ((str[2] == '\\' || str[2] == '/') && str[3] == '\0'));
#else
    (void)str;
    return false;
#endif
}

int platform_get_available_drives(char *out_drives, int max_drives)
{
    if (!out_drives || max_drives <= 0) return 0;
    
#if NCD_PLATFORM_WINDOWS
    DWORD mask = GetLogicalDrives();
    int count = 0;
    for (int i = 0; i < 26 && count < max_drives; i++) {
        if (mask & (1u << i)) {
            out_drives[count++] = (char)('A' + i);
        }
    }
    return count;
#else
    /* On Linux, enumerate mounts and return assigned letters */
    char mnt_bufs[26][MAX_PATH];
    const char *mnt_ptrs[26];
    int count = ncd_platform_enumerate_mounts(mnt_bufs, mnt_ptrs, MAX_PATH, max_drives);
    
    for (int i = 0; i < count && i < max_drives; i++) {
        char letter = platform_get_drive_letter(mnt_ptrs[i]);
        out_drives[i] = letter ? letter : (char)(i + 1);
    }
    return count;
#endif
}

int platform_filter_available_drives(const char *in_drives, int in_count,
                                      const bool skip_mask[26],
                                      char *out_drives, int max_out)
{
    if (!in_drives || !out_drives || max_out <= 0) return 0;
    
    int out_idx = 0;
    for (int i = 0; i < in_count && out_idx < max_out; i++) {
        char letter = in_drives[i];
        int idx = -1;
        if (isalpha((unsigned char)letter)) {
            idx = (int)(char)toupper((unsigned char)letter) - 'A';
        } else if (letter >= 1 && letter <= 26) {
            idx = (int)letter - 1;
        }
        if (idx >= 0 && idx < 26 && skip_mask[idx]) continue;
        out_drives[out_idx++] = letter;
    }
    return out_idx;
}

/* ================================================================ Case-insensitive substring search */

const char *platform_strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return NULL;
    if (!needle[0]) return haystack;
    
#if NCD_PLATFORM_WINDOWS
    /* Use Windows Shlwapi function */
    return (const char *)StrStrIA(haystack, needle);
#else
    /* Use POSIX.1-2008 function on Linux */
    return strcasestr(haystack, needle);
#endif
}

int platform_strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    if (n == 0) return 0;
    
#if NCD_PLATFORM_WINDOWS
    return _strnicmp(s1, s2, n);
#else
    return strncasecmp(s1, s2, n);
#endif
}

/* ================================================================ console output */

static PlatformHandle g_ncd_con = NULL;
static bool g_ncd_con_initialized = false;

static void ncd_con_init(void)
{
    if (!g_ncd_con_initialized) {
        g_ncd_con = platform_console_open_out(false);
        g_ncd_con_initialized = true;
    }
}

void ncd_print(const char *s)
{
    if (!g_ncd_con_initialized) ncd_con_init();
    if (g_ncd_con) {
        platform_console_write(g_ncd_con, s);
    } else {
        fputs(s, stdout);
        fflush(stdout);
    }
}

void ncd_println(const char *s)
{
    ncd_print(s);
    ncd_print("\r\n");
}

void ncd_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ncd_print(buf);
}
