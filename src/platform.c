/*
 * platform.c -- NCD-specific extensions to the shared platform library
 */

#include "platform.h"
#include "ncd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if NCD_PLATFORM_LINUX
#include <errno.h>
#endif

/* NCD-specific pseudo filesystems list (from original platform.c) */
static const char *ncd_pseudo_filesystems[] = {
    "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "securityfs",
    "cgroup", "cgroup2", "pstore", "bpf", "autofs", "hugetlbfs",
    "mqueue", "debugfs", "tracefs", "ramfs", "squashfs", "overlay",
    "fusectl", "nsfs", "efivarfs", "configfs", "selinuxfs", "pipefs",
    "sockfs", "rpc_pipefs", "nfsd", "sunrpc", "cpuset", "xenfs",
    "fuse.portal", "fuse.gvfsd-fuse", NULL
};

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

        /* Skip pseudo filesystems using NCD's list */
        bool is_pseudo = false;
        for (int i = 0; ncd_pseudo_filesystems[i]; i++) {
            if (strcmp(fstype, ncd_pseudo_filesystems[i]) == 0) {
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
