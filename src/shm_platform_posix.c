/*
 * shm_platform_posix.c  --  POSIX shared memory implementation
 *
 * Uses POSIX shared memory:
 * - shm_open for creating/opening shared memory objects
 * - ftruncate for sizing
 * - mmap for mapping into address space
 * - shm_unlink for removing
 */

#include "shm_platform.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------- internal types     */

struct ShmHandle {
    int    fd;                   /* File descriptor */
    char   name[256];            /* Object name (including leading /) */
    size_t size;                 /* Object size */
    bool   is_creator;           /* True if created (vs opened) */
};

/* --------------------------------------------------------- globals            */

static int g_last_errno = 0;

/* --------------------------------------------------------- error handling     */

static void set_last_errno(int err) {
    g_last_errno = err;
}

const char *shm_error_string(ShmResult result) {
    switch (result) {
        case SHM_OK: return "Success";
        case SHM_ERROR_GENERIC: return "Generic error";
        case SHM_ERROR_NOMEM: return "Out of memory";
        case SHM_ERROR_NOTFOUND: return "Object not found";
        case SHM_ERROR_EXISTS: return "Object already exists";
        case SHM_ERROR_ACCESS: return "Permission denied";
        case SHM_ERROR_SIZE: return "Invalid size";
        case SHM_ERROR_BUSY: return "Object in use";
        default: return "Unknown error";
    }
}

static ShmResult errno_to_shm(int err) {
    switch (err) {
        case 0:
            return SHM_OK;
        case ENOENT:
            return SHM_ERROR_NOTFOUND;
        case EEXIST:
            return SHM_ERROR_EXISTS;
        case EACCES:
        case EPERM:
            return SHM_ERROR_ACCESS;
        case ENOMEM:
            return SHM_ERROR_NOMEM;
        case EBUSY:
            return SHM_ERROR_BUSY;
        case EINVAL:
            return SHM_ERROR_SIZE;
        default:
            return SHM_ERROR_GENERIC;
    }
}

/* --------------------------------------------------------- lifecycle          */

ShmResult shm_platform_init(void) {
    /* Nothing special needed on POSIX */
    return SHM_OK;
}

void shm_platform_cleanup(void) {
    /* Nothing special needed on POSIX */
}

/* --------------------------------------------------------- object management  */

ShmResult shm_create(const char *name, size_t size, ShmHandle **out_handle) {
    if (!name || !out_handle || size == 0) {
        return SHM_ERROR_GENERIC;
    }
    
    *out_handle = NULL;
    
    /* Validate size */
    if (size == 0) {
        return SHM_ERROR_SIZE;
    }
    
    /* Allocate handle structure */
    ShmHandle *handle = (ShmHandle *)calloc(1, sizeof(ShmHandle));
    if (!handle) {
        return SHM_ERROR_NOMEM;
    }
    
    /* Store name (ensure it starts with /) */
    if (name[0] == '/') {
        strncpy(handle->name, name, sizeof(handle->name) - 1);
    } else {
        snprintf(handle->name, sizeof(handle->name), "/%s", name);
    }
    handle->name[sizeof(handle->name) - 1] = '\0';
    handle->size = size;
    handle->is_creator = true;
    
    /* Create shared memory object */
    /* O_EXCL ensures we fail if it already exists */
    handle->fd = shm_open(handle->name, O_CREAT | O_EXCL | O_RDWR, 0660);
    if (handle->fd == -1) {
        set_last_errno(errno);
        free(handle);
        return errno_to_shm(errno);
    }
    
    /* Set size */
    if (ftruncate(handle->fd, (off_t)size) == -1) {
        set_last_errno(errno);
        close(handle->fd);
        shm_unlink(handle->name);
        free(handle);
        return errno_to_shm(errno);
    }
    
    *out_handle = handle;
    return SHM_OK;
}

ShmResult shm_open_existing(const char *name, ShmAccess access, ShmHandle **out_handle) {
    if (!name || !out_handle) {
        return SHM_ERROR_GENERIC;
    }
    
    *out_handle = NULL;
    
    /* Allocate handle structure */
    ShmHandle *handle = (ShmHandle *)calloc(1, sizeof(ShmHandle));
    if (!handle) {
        return SHM_ERROR_NOMEM;
    }
    
    /* Store name (ensure it starts with /) */
    if (name[0] == '/') {
        strncpy(handle->name, name, sizeof(handle->name) - 1);
    } else {
        snprintf(handle->name, sizeof(handle->name), "/%s", name);
    }
    handle->name[sizeof(handle->name) - 1] = '\0';
    handle->is_creator = false;
    
    /* Determine access mode */
    int flags = (access == SHM_ACCESS_WRITE) ? O_RDWR : O_RDONLY;
    
    /* Open existing shared memory object */
    handle->fd = shm_open(handle->name, flags, 0);
    if (handle->fd == -1) {
        set_last_errno(errno);
        free(handle);
        return errno_to_shm(errno);
    }
    
    /* Get size */
    struct stat st;
    if (fstat(handle->fd, &st) == -1) {
        set_last_errno(errno);
        close(handle->fd);
        free(handle);
        return errno_to_shm(errno);
    }
    handle->size = (size_t)st.st_size;
    
    *out_handle = handle;
    return SHM_OK;
}

ShmResult shm_open(const char *name, ShmAccess access, ShmHandle **out_handle) {
    return shm_open_existing(name, access, out_handle);
}

void shm_close(ShmHandle *handle) {
    if (!handle) {
        return;
    }
    
    if (handle->fd != -1) {
        close(handle->fd);
    }
    
    free(handle);
}

ShmResult shm_unlink(const char *name) {
    if (!name) {
        return SHM_ERROR_GENERIC;
    }
    
    char full_name[256];
    if (name[0] == '/') {
        strncpy(full_name, name, sizeof(full_name) - 1);
    } else {
        snprintf(full_name, sizeof(full_name), "/%s", name);
    }
    full_name[sizeof(full_name) - 1] = '\0';
    
    if (shm_unlink(full_name) == -1) {
        return errno_to_shm(errno);
    }
    
    return SHM_OK;
}

/* --------------------------------------------------------- memory mapping     */

ShmResult shm_map(ShmHandle *handle, ShmAccess access, 
                  void **out_addr, size_t *out_size) {
    if (!handle || !out_addr || !out_size) {
        return SHM_ERROR_GENERIC;
    }
    
    *out_addr = NULL;
    *out_size = 0;
    
    if (handle->fd == -1) {
        return SHM_ERROR_GENERIC;
    }
    
    /* Determine protection */
    int prot = PROT_READ;
    if (access == SHM_ACCESS_WRITE) {
        prot |= PROT_WRITE;
    }
    
    /* Map the shared memory */
    void *addr = mmap(NULL, handle->size, prot, MAP_SHARED, handle->fd, 0);
    if (addr == MAP_FAILED) {
        set_last_errno(errno);
        return errno_to_shm(errno);
    }
    
    *out_addr = addr;
    *out_size = handle->size;
    
    return SHM_OK;
}

ShmResult shm_unmap(void *addr, size_t size) {
    if (!addr) {
        return SHM_ERROR_GENERIC;
    }
    
    if (munmap(addr, size) == -1) {
        return errno_to_shm(errno);
    }
    
    return SHM_OK;
}

/* --------------------------------------------------------- utilities          */

size_t shm_get_size(const ShmHandle *handle) {
    if (!handle) {
        return 0;
    }
    return handle->size;
}

const char *shm_get_name(const ShmHandle *handle) {
    if (!handle) {
        return NULL;
    }
    return handle->name;
}

bool shm_exists(const char *name) {
    if (!name) {
        return false;
    }
    
    char full_name[256];
    if (name[0] == '/') {
        strncpy(full_name, name, sizeof(full_name) - 1);
    } else {
        snprintf(full_name, sizeof(full_name), "/%s", name);
    }
    full_name[sizeof(full_name) - 1] = '\0';
    
    /* Try to open read-only */
    int fd = shm_open(full_name, O_RDONLY, 0);
    if (fd == -1) {
        return false;
    }
    
    close(fd);
    return true;
}

size_t shm_get_page_size(void) {
    return (size_t)sysconf(_SC_PAGESIZE);
}

size_t shm_round_up_size(size_t size) {
    size_t page_size = shm_get_page_size();
    return (size + page_size - 1) & ~(page_size - 1);
}

/* --------------------------------------------------------- naming             */

bool shm_make_name(const char *base, char *out_buf, size_t buf_size) {
    if (!base || !out_buf || buf_size == 0) {
        return false;
    }
    
    /* Get user ID */
    uid_t uid = getuid();
    
    /* Format: /ncd_<uid>_<base> */
    /* Note: POSIX names should start with / but not contain other / */
    int written = snprintf(out_buf, buf_size, "/ncd_%d_%s", (int)uid, base);
    
    return (written > 0 && (size_t)written < buf_size);
}

bool shm_make_meta_name(char *out_buf, size_t buf_size) {
    return shm_make_name("metadata", out_buf, buf_size);
}

bool shm_make_db_name(char *out_buf, size_t buf_size) {
    return shm_make_name("database", out_buf, buf_size);
}

bool shm_make_ctl_name(char *out_buf, size_t buf_size) {
    return shm_make_name("control", out_buf, buf_size);
}
