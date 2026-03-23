/*
 * platform.h -- NCD wrapper around shared platform library
 *
 * This file extends the shared platform.h with NCD-specific functionality.
 */

#ifndef NCD_PLATFORM_H
#define NCD_PLATFORM_H

/* Include shared platform library */
#include "../shared/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================ NCD-specific database paths
 *
 * These functions extend the shared platform library with NCD-specific
 * database path functionality.
 */

/* Get the default database file path for NCD */
bool ncd_platform_db_default_path(char *buf, size_t buf_size);

/* Get the per-drive database path for NCD */
bool ncd_platform_db_drive_path(char letter, char *buf, size_t buf_size);

/* ================================================================ NCD-specific mount enumeration
 *
 * The NCD version filters out pseudo filesystems specific to NCD's use case.
 */

/* Extended mount enumeration with NCD pseudo filesystem filtering */
int ncd_platform_enumerate_mounts(char mount_bufs[][MAX_PATH],
                                  const char *mount_ptrs[],
                                  size_t buf_size,
                                  int max_mounts);

/* Check if a filesystem type is a pseudo filesystem (proc, sysfs, tmpfs, etc.) */
bool platform_is_pseudo_fs(const char *fstype);

/* ================================================================ NCD-specific platform abstractions
 *
 * These functions abstract platform-specific operations to reduce #if blocks in main code.
 */

/* 
 * Parse drive letter from search string and extract clean search term.
 * Returns drive letter (A-Z on Windows, 0x01-0x1A on Linux) and writes clean search to out_search.
 */
char platform_parse_drive_from_search(const char *search, char *out_search, size_t out_size);

/*
 * Build mount path from drive letter.
 * Windows: "C:\", Linux: "/mnt/X" or the actual mount point
 */
bool platform_build_mount_path(char letter, char *out_path, size_t out_size);

/*
 * Check if the string specifies a drive root (e.g., "C:" or "C:\" on Windows).
 */
bool platform_is_drive_specifier(const char *str);

/*
 * Get the list of available drives as character codes.
 * Returns number of drives written to out_drives (max 26).
 */
int platform_get_available_drives(char *out_drives, int max_drives);

/*
 * Filter available drives based on skip mask.
 * Takes input drives from platform_get_available_drives and filters them.
 */
int platform_filter_available_drives(const char *in_drives, int in_count, 
                                      const bool skip_mask[26],
                                      char *out_drives, int max_out);

/*
 * Get the application title string for help/version display.
 */
const char *platform_get_app_title(void);

/*
 * Write the platform-specific help suffix (e.g., Linux-specific options).
 * Returns number of characters written, or 0 if none.
 */
int platform_write_help_suffix(char *buf, size_t buf_size);

/*
 * Case-insensitive substring search.
 * Returns pointer to first occurrence of needle in haystack (case-insensitive),
 * or NULL if not found.
 */
const char *platform_strcasestr(const char *haystack, const char *needle);

#ifdef __cplusplus
}
#endif

#endif /* NCD_PLATFORM_H */
