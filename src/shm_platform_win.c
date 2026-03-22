/*
 * shm_platform_win.c  --  Windows shared memory implementation
 *
 * Uses Windows memory-mapped files:
 * - CreateFileMapping with INVALID_HANDLE_VALUE for unnamed file mapping
 * - MapViewOfFile for mapping into address space
 */

#include "shm_platform.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------- internal types     */

struct ShmHandle {
    HANDLE hMapFile;             /* File mapping handle */
    char   name[MAX_PATH];       /* Object name */
    size_t size;                 /* Requested size */
    bool   is_creator;           /* True if created (vs opened) */
};

/* --------------------------------------------------------- globals            */

static DWORD g_last_error = 0;

/* --------------------------------------------------------- error handling     */

static void set_last_error(DWORD err) {
    g_last_error = err;
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

static ShmResult win_error_to_shm(DWORD err) {
    switch (err) {
        case ERROR_SUCCESS:
            return SHM_OK;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return SHM_ERROR_NOTFOUND;
        case ERROR_ALREADY_EXISTS:
            return SHM_ERROR_EXISTS;
        case ERROR_ACCESS_DENIED:
            return SHM_ERROR_ACCESS;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return SHM_ERROR_NOMEM;
        case ERROR_BUSY:
        case ERROR_LOCK_VIOLATION:
            return SHM_ERROR_BUSY;
        default:
            return SHM_ERROR_GENERIC;
    }
}

/* --------------------------------------------------------- lifecycle          */

ShmResult shm_platform_init(void) {
    /* Nothing special needed on Windows */
    return SHM_OK;
}

void shm_platform_cleanup(void) {
    /* Nothing special needed on Windows */
}

/* --------------------------------------------------------- object management  */

ShmResult shm_create(const char *name, size_t size, ShmHandle **out_handle) {
    if (!name || !out_handle || size == 0) {
        return SHM_ERROR_GENERIC;
    }
    
    *out_handle = NULL;
    
    /* Validate size doesn't exceed DWORD */
    if (size > 0xFFFFFFFF) {
        return SHM_ERROR_SIZE;
    }
    
    /* Allocate handle structure */
    ShmHandle *handle = (ShmHandle *)calloc(1, sizeof(ShmHandle));
    if (!handle) {
        return SHM_ERROR_NOMEM;
    }
    
    strncpy(handle->name, name, MAX_PATH - 1);
    handle->name[MAX_PATH - 1] = '\0';
    handle->size = size;
    handle->is_creator = true;
    
    /* Create file mapping */
    handle->hMapFile = CreateFileMapping(
        INVALID_HANDLE_VALUE,    /* Use paging file */
        NULL,                    /* Default security */
        PAGE_READWRITE,          /* Read/write access */
        0,                       /* Maximum object size (high-order DWORD) */
        (DWORD)size,             /* Maximum object size (low-order DWORD) */
        name                     /* Name of mapping object */
    );
    
    if (handle->hMapFile == NULL) {
        set_last_error(GetLastError());
        free(handle);
        return win_error_to_shm(g_last_error);
    }
    
    /* Check if object already existed */
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Close the handle we just got */
        CloseHandle(handle->hMapFile);
        
        /* Try to open and close the existing object to potentially clean it up */
        HANDLE hExisting = OpenFileMapping(FILE_MAP_WRITE, FALSE, name);
        if (hExisting != NULL) {
            CloseHandle(hExisting);
            /* Brief delay to let Windows clean up if this was the last handle */
            Sleep(10);
            
            /* Retry create */
            handle->hMapFile = CreateFileMapping(
                INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                0,
                (DWORD)size,
                name
            );
            
            if (handle->hMapFile != NULL && GetLastError() != ERROR_ALREADY_EXISTS) {
                /* Success on retry */
                *out_handle = handle;
                return SHM_OK;
            }
            
            if (handle->hMapFile != NULL) {
                CloseHandle(handle->hMapFile);
            }
        }
        
        free(handle);
        return SHM_ERROR_EXISTS;
    }
    
    *out_handle = handle;
    return SHM_OK;
}

ShmResult shm_open(const char *name, ShmAccess access, ShmHandle **out_handle) {
    if (!name || !out_handle) {
        return SHM_ERROR_GENERIC;
    }
    
    *out_handle = NULL;
    
    /* Allocate handle structure */
    ShmHandle *handle = (ShmHandle *)calloc(1, sizeof(ShmHandle));
    if (!handle) {
        return SHM_ERROR_NOMEM;
    }
    
    strncpy(handle->name, name, MAX_PATH - 1);
    handle->name[MAX_PATH - 1] = '\0';
    handle->is_creator = false;
    
    /* Open existing file mapping */
    DWORD desired_access = (access == SHM_ACCESS_WRITE) ? 
        FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
    
    handle->hMapFile = OpenFileMapping(
        desired_access,          /* Access mode */
        FALSE,                   /* Do not inherit the name */
        name                     /* Name of mapping object */
    );
    
    if (handle->hMapFile == NULL) {
        set_last_error(GetLastError());
        free(handle);
        return win_error_to_shm(g_last_error);
    }
    
    /* Get actual size */
    /* Note: Windows doesn't provide direct API to get size from handle.
     * We need to map it first to determine size. */
    
    *out_handle = handle;
    return SHM_OK;
}

void shm_close(ShmHandle *handle) {
    if (!handle) {
        return;
    }
    
    if (handle->hMapFile != NULL) {
        CloseHandle(handle->hMapFile);
    }
    
    free(handle);
}

ShmResult shm_unlink(const char *name) {
    if (!name) {
        return SHM_ERROR_GENERIC;
    }
    
    /* On Windows, try to open and close the existing mapping to force cleanup.
     * This helps when a previous process crashed and left the object around. */
    HANDLE hMap = OpenFileMapping(FILE_MAP_WRITE, FALSE, name);
    if (hMap != NULL) {
        /* Object exists - closing this handle might destroy it if we're the last */
        CloseHandle(hMap);
        /* Windows doesn't have a direct "delete" for file mappings.
         * They are destroyed when the last handle closes.
         * We successfully "unlinked" (removed our reference). */
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
    
    if (handle->hMapFile == NULL) {
        return SHM_ERROR_GENERIC;
    }
    
    /* Determine access mode */
    DWORD file_map_access = (access == SHM_ACCESS_WRITE) ? 
        FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
    
    /* Map view of file */
    LPVOID pBuf = MapViewOfFile(
        handle->hMapFile,        /* Handle to map object */
        file_map_access,         /* Access mode */
        0,                       /* High-order DWORD of file offset */
        0,                       /* Low-order DWORD of file offset */
        0                        /* Map entire file (0 = to end) */
    );
    
    if (pBuf == NULL) {
        set_last_error(GetLastError());
        return win_error_to_shm(g_last_error);
    }
    
    /* Get actual size - we need to use VirtualQuery */
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(pBuf, &mbi, sizeof(mbi)) == 0) {
        UnmapViewOfFile(pBuf);
        return SHM_ERROR_GENERIC;
    }
    
    *out_addr = pBuf;
    *out_size = mbi.RegionSize;
    
    return SHM_OK;
}

ShmResult shm_unmap(void *addr, size_t size) {
    (void)size;  /* Not used on Windows */
    
    if (!addr) {
        return SHM_ERROR_GENERIC;
    }
    
    if (!UnmapViewOfFile(addr)) {
        return win_error_to_shm(GetLastError());
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
    
    /* Try to open with minimal access */
    HANDLE hMap = OpenFileMapping(FILE_MAP_READ, FALSE, name);
    if (hMap == NULL) {
        return false;
    }
    
    CloseHandle(hMap);
    return true;
}

size_t shm_get_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
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
    
    /* Get current session ID for scoping */
    DWORD session_id = WTSGetActiveConsoleSessionId();
    if (session_id == 0xFFFFFFFF) {
        session_id = 0;
    }
    
    /* Get current process ID for additional uniqueness */
    DWORD pid = GetCurrentProcessId();
    
    /* Format: Local\NCD_<session>_<pid>_base */
    /* Note: Using Local\ prefix for session-local (non-global) namespace */
    int written = snprintf(out_buf, buf_size, "Local\\NCD_%lu_%lu_%s", 
                          session_id, pid, base);
    
    return (written > 0 && (size_t)written < buf_size);
}

/* For service-based naming, we need stable names that don't include PID.
 * These versions use only user identity. */

static bool shm_make_stable_name(const char *base, char *out_buf, size_t buf_size) {
    if (!base || !out_buf || buf_size == 0) {
        return false;
    }
    
    /* Get user SID for stable per-user naming */
    HANDLE hToken = NULL;
    char sid_string[256] = {0};
    
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        DWORD dwLength = 0;
        GetTokenInformation(hToken, TokenUser, NULL, 0, &dwLength);
        
        if (dwLength > 0) {
            PTOKEN_USER pTokenUser = (PTOKEN_USER)malloc(dwLength);
            if (pTokenUser && GetTokenInformation(hToken, TokenUser, 
                                                  pTokenUser, dwLength, &dwLength)) {
                /* Convert SID to string */
                LPSTR sidStr = NULL;
                if (ConvertSidToStringSidA(pTokenUser->User.Sid, &sidStr)) {
                    strncpy(sid_string, sidStr, sizeof(sid_string) - 1);
                    LocalFree(sidStr);
                }
            }
            free(pTokenUser);
        }
        CloseHandle(hToken);
    }
    
    /* If we couldn't get SID, use session ID as fallback */
    if (sid_string[0] == '\0') {
        DWORD session_id = WTSGetActiveConsoleSessionId();
        if (session_id == 0xFFFFFFFF) {
            session_id = 0;
        }
        snprintf(sid_string, sizeof(sid_string), "S-%lu", session_id);
    }
    
    /* Format: Local\NCD_<SID>_base */
    int written = snprintf(out_buf, buf_size, "Local\\NCD_%s_%s", sid_string, base);
    
    return (written > 0 && (size_t)written < buf_size);
}

bool shm_make_meta_name(char *out_buf, size_t buf_size) {
    return shm_make_stable_name("metadata", out_buf, buf_size);
}

bool shm_make_db_name(char *out_buf, size_t buf_size) {
    return shm_make_stable_name("database", out_buf, buf_size);
}

bool shm_make_ctl_name(char *out_buf, size_t buf_size) {
    return shm_make_stable_name("control", out_buf, buf_size);
}
