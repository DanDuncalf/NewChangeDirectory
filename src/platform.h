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

#ifdef __cplusplus
}
#endif

#endif /* NCD_PLATFORM_H */
