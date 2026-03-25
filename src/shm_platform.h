/*
 * shm_platform.h  --  Cross-platform shared memory abstraction
 *
 * This header provides a unified interface for shared memory operations
 * on both Windows and Linux/POSIX platforms.
 */

#ifndef NCD_SHM_PLATFORM_H
#define NCD_SHM_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --------------------------------------------------------- types              */

/* Opaque handle to a shared memory object */
typedef struct ShmHandle ShmHandle;

/* Platform-specific context (hidden implementation) */
typedef struct ShmContext ShmContext;

/* Result codes */
typedef enum {
    SHM_OK = 0,                  /* Success */
    SHM_ERROR_GENERIC = -1,      /* Generic error */
    SHM_ERROR_NOMEM = -2,        /* Out of memory */
    SHM_ERROR_NOTFOUND = -3,     /* Object not found */
    SHM_ERROR_EXISTS = -4,       /* Object already exists */
    SHM_ERROR_ACCESS = -5,       /* Permission denied */
    SHM_ERROR_SIZE = -6,         /* Invalid size */
    SHM_ERROR_BUSY = -7,         /* Object in use */
} ShmResult;

/* Access mode for opening */
typedef enum {
    SHM_ACCESS_READ,             /* Read-only access */
    SHM_ACCESS_WRITE,            /* Read-write access */
} ShmAccess;

/* --------------------------------------------------------- lifecycle          */

/*
 * shm_platform_init  --  Initialize shared memory subsystem
 *
 * Must be called before any other shm_platform functions.
 * Returns SHM_OK on success.
 */
ShmResult shm_platform_init(void);

/*
 * shm_platform_cleanup  --  Cleanup shared memory subsystem
 *
 * Should be called when done using shared memory.
 */
void shm_platform_cleanup(void);

/* --------------------------------------------------------- object management  */

/*
 * shm_create  --  Create a new shared memory object
 *
 * name: Platform-specific name (will be scoped to user/session)
 * size: Size in bytes (must be > 0)
 * out_handle: Receives handle on success
 *
 * Returns SHM_OK on success. Caller must call shm_close() when done.
 */
ShmResult shm_create(const char *name, size_t size, ShmHandle **out_handle);

/*
 * shm_open_existing  --  Open an existing shared memory object
 *
 * name: Name of existing object
 * access: Desired access mode
 * out_handle: Receives handle on success
 *
 * Returns SHM_OK on success. Caller must call shm_close() when done.
 */
ShmResult shm_open_existing(const char *name, ShmAccess access, ShmHandle **out_handle);

/*
 * shm_close  --  Close a shared memory handle
 *
 * This unmaps the memory and closes the handle. For objects created with
 * shm_create(), the object persists until shm_remove() is called.
 */
void shm_close(ShmHandle *handle);

/*
 * shm_remove  --  Remove a shared memory object
 *
 * name: Name of object to remove
 *
 * Removes the object from the system. Existing mappings remain valid
 * until closed. Returns SHM_OK on success.
 */
ShmResult shm_remove(const char *name);

/* --------------------------------------------------------- memory mapping     */

/*
 * shm_map  --  Map shared memory into address space
 *
 * handle: Valid handle from shm_create() or shm_open()
 * access: Desired access (should match or be more restrictive than open)
 * out_addr: Receives mapped address on success
 * out_size: Receives actual mapped size on success
 *
 * Returns SHM_OK on success. Caller must call shm_unmap() when done.
 */
ShmResult shm_map(ShmHandle *handle, ShmAccess access, 
                  void **out_addr, size_t *out_size);

/*
 * shm_unmap  --  Unmap shared memory
 *
 * addr: Address returned by shm_map()
 * size: Size returned by shm_map()
 *
 * Returns SHM_OK on success.
 */
ShmResult shm_unmap(void *addr, size_t size);

/* --------------------------------------------------------- utilities          */

/*
 * shm_get_size  --  Get size of shared memory object
 *
 * Returns the size in bytes, or 0 on error.
 */
size_t shm_get_size(const ShmHandle *handle);

/*
 * shm_get_name  --  Get name of shared memory object
 *
 * Returns the name, or NULL on error. Pointer is valid until handle closed.
 */
const char *shm_get_name(const ShmHandle *handle);

/*
 * shm_error_string  --  Get human-readable error message
 *
 * Returns static string describing the last error.
 */
const char *shm_error_string(ShmResult result);

/* --------------------------------------------------------- naming             */

/*
 * shm_make_name  --  Create per-user scoped shared memory name
 *
 * base: Base name (e.g., "ncd_metadata")
 * out_buf: Output buffer
 * buf_size: Size of output buffer
 *
 * Generates a platform-appropriate name scoped to the current user.
 * On Windows: "Local\NCD_<SID>_base"
 * On Linux: "/ncd_<uid>_base"
 *
 * Returns true on success.
 */
bool shm_make_name(const char *base, char *out_buf, size_t buf_size);

/*
 * shm_make_meta_name  --  Get metadata snapshot name
 */
bool shm_make_meta_name(char *out_buf, size_t buf_size);

/*
 * shm_make_db_name  --  Get database snapshot name
 */
bool shm_make_db_name(char *out_buf, size_t buf_size);

/*
 * shm_make_ctl_name  --  Get control channel name
 */
bool shm_make_ctl_name(char *out_buf, size_t buf_size);

/* --------------------------------------------------------- service helpers    */

/*
 * shm_exists  --  Check if a shared memory object exists
 *
 * Returns true if the object exists and is accessible.
 */
bool shm_exists(const char *name);

/*
 * shm_get_page_size  --  Get system page size
 *
 * Returns the platform page size (e.g., 4096 bytes).
 */
size_t shm_get_page_size(void);

/*
 * shm_round_up_size  --  Round size up to page boundary
 *
 * Returns size rounded up to the next multiple of page size.
 */
size_t shm_round_up_size(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NCD_SHM_PLATFORM_H */
