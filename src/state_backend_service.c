/*
 * state_backend_service.c  --  Service-backed state backend implementation
 *
 * This implements the state_backend interface by connecting to the
 * NCD state service and mapping shared memory snapshots.
 */

#include "state_backend.h"
#include "shared_state.h"
#include "shm_platform.h"
#include "shm_types.h"
#include "control_ipc.h"
#include "database.h"
#include "platform.h"
#include "service_state.h"  /* For ServiceRuntimeState enum */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Build version - must match NCD_APP_VERSION in control_ipc.h */
#ifndef NCD_BUILD_VER
#define NCD_BUILD_VER   "1.3"
#endif
#ifndef NCD_BUILD_STAMP
#define NCD_BUILD_STAMP __DATE__ " " __TIME__
#endif

/* --------------------------------------------------------- internal state     */

static char g_last_error[256] = {0};

/* Convenience macros for accessing service mode fields */
#define SERVICE(view) ((view)->data.service)

/* --------------------------------------------------------- error handling     */

static void set_error(const char *msg) {
    strncpy(g_last_error, msg, sizeof(g_last_error) - 1);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
}

/* --------------------------------------------------------- snapshot loading   */

/*
 * ZERO-COPY metadata loading.
 * 
 * Instead of copying all metadata into a local structure, we store the base
 * pointer to the shared memory snapshot and use offset-based access macros
 * to read data directly from shared memory.
 */
static bool load_metadata_from_snapshot(NcdStateView *view) {
    if (!SERVICE(view).meta_addr) {
        set_error("Metadata not mapped");
        return false;
    }
    
    /* Validate header using new shm_types format */
    const ShmMetadataHeader *hdr = (const ShmMetadataHeader *)SERVICE(view).meta_addr;
    
    if (hdr->magic != NCD_SHM_META_MAGIC) {
        set_error("Invalid metadata snapshot magic");
        return false;
    }
    
    if (hdr->version != NCD_SHM_TYPES_VERSION) {
        set_error("Unsupported metadata snapshot version");
        return false;
    }
    
    /* Validate checksum */
    if (!shm_validate_checksum(SERVICE(view).meta_addr, SERVICE(view).meta_size)) {
        set_error("Metadata snapshot checksum mismatch");
        return false;
    }
    
    /* Validate text encoding matches platform */
    uint32_t expected_encoding = NCD_SHM_TEXT_ENCODING;
    if (hdr->text_encoding != 0 && hdr->text_encoding != expected_encoding) {
        set_error("Metadata encoding mismatch");
        return false;
    }
    
    /* Store the metadata pointer for zero-copy access */
    SERVICE(view).metadata_ptr = SERVICE(view).meta_addr;
    
    /* 
     * ZERO-COPY: We don't copy the metadata here.
     * Instead, state_view_metadata_service() will use SHM_PTR macros
     * to access data directly from shared memory.
     */
    
    SERVICE(view).has_metadata = true;
    return true;
}

/*
 * ZERO-COPY database loading.
 * 
 * Sets up the database_view to point directly into shared memory.
 * The DirEntry arrays and name pools are accessed via offsets from the base.
 */
static bool load_database_from_snapshot(NcdStateView *view) {
    if (!SERVICE(view).db_addr) {
        set_error("Database not mapped");
        return false;
    }
    
    /* Validate header using new shm_types format */
    const ShmDatabaseHeader *hdr = (const ShmDatabaseHeader *)SERVICE(view).db_addr;
    
    if (hdr->magic != NCD_SHM_DB_MAGIC) {
        set_error("Invalid database snapshot magic");
        return false;
    }
    
    if (hdr->version != NCD_SHM_TYPES_VERSION) {
        set_error("Unsupported database snapshot version");
        return false;
    }
    
    /* Validate checksum */
    if (!shm_validate_checksum(SERVICE(view).db_addr, SERVICE(view).db_size)) {
        set_error("Database snapshot checksum mismatch");
        return false;
    }
    
    /* Validate text encoding matches platform */
    uint32_t expected_encoding = NCD_SHM_TEXT_ENCODING;
    if (hdr->text_encoding != 0 && hdr->text_encoding != expected_encoding) {
        set_error("Database encoding mismatch");
        return false;
    }
    
    /* Store the database pointer for zero-copy access */
    SERVICE(view).database_ptr = SERVICE(view).db_addr;
    
    /* Set up minimal database_view pointing to shared memory */
    if (!SERVICE(view).database_view) {
        SERVICE(view).database_view = (NcdDatabase *)calloc(1, sizeof(NcdDatabase));
        if (!SERVICE(view).database_view) {
            set_error("Failed to allocate database view");
            return false;
        }
    }
    
    NcdDatabase *db = SERVICE(view).database_view;
    db->version = (int)hdr->version;
    db->last_scan = (time_t)hdr->last_scan;
    db->default_show_hidden = hdr->show_hidden;
    db->default_show_system = hdr->show_system;
    db->drive_count = (int)hdr->mount_count;
    
    if (hdr->mount_count == 0) {
        /* Empty database - valid but has no drives */
        SERVICE(view).has_database = true;
        return true;
    }
    
    if (hdr->mount_count > NCD_SHM_MAX_DRIVES) {
        set_error("Too many mounts in snapshot");
        return false;
    }
    
    /* Allocate DriveData array */
    db->drives = (DriveData *)ncd_malloc_array(hdr->mount_count, sizeof(DriveData));
    if (!db->drives) {
        set_error("Failed to allocate drive array");
        return false;
    }
    db->drive_capacity = (int)hdr->mount_count;
    
    /* Mount entries array follows header */
    size_t mount_table_offset = sizeof(ShmDatabaseHeader);
    const ShmMountEntry *mounts = (const ShmMountEntry *)((uint8_t *)SERVICE(view).db_addr + mount_table_offset);
    
    /* Set up each drive to point directly into shared memory */
    for (uint32_t i = 0; i < hdr->mount_count; i++) {
        DriveData *drv = &db->drives[i];
        const ShmMountEntry *mount = &mounts[i];
        
        /* Copy mount point (platform-native encoding) */
        const NcdShmChar *mp = SHM_MOUNT_POINT(SERVICE(view).db_addr, mount);
        shm_strncpy(drv->mount_point, mp, NCD_MAX_PATH - 1);
        drv->mount_point[NCD_MAX_PATH - 1] = 0;
        
        /* Copy volume label (platform-native encoding) */
        const NcdShmChar *vl = SHM_VOLUME_LABEL(SERVICE(view).db_addr, mount);
        shm_strncpy(drv->volume_label, vl, 63);
        drv->volume_label[63] = 0;
        
        /* Legacy fields */
        drv->letter = (char)(drv->mount_point[0]);  /* First char of mount point */
        
        /* Set up directory entries - point directly into shared memory */
        drv->dir_count = (int)mount->dir_count;
        drv->dir_capacity = (int)mount->dir_count;
        drv->dirs = (DirEntry *)SHM_MOUNT_DIRS(SERVICE(view).db_addr, mount);
        drv->name_pool = (char *)SHM_MOUNT_POOL(SERVICE(view).db_addr, mount);
        drv->name_pool_len = mount->pool_size;
        drv->name_pool_cap = mount->pool_size;
    }
    
    /* Mark as blob-style so free logic knows not to free individual pools */
    db->is_blob = true;
    db->blob_buf = NULL;
    
    SERVICE(view).has_database = true;
    return true;
}

/* --------------------------------------------------------- lifecycle          */

/* Forward declarations for helper functions */
static bool connect_with_retry(NcdIpcClient **client, int max_retries, int retry_delay_ms);
static bool wait_for_service_ready(NcdIpcClient *client, int timeout_ms);

/* Debug logging for state backend service - uses NCD_DEBUG_LOG from ncd.h */
#ifdef NCD_DEBUG_LOG
#define SVC_DBG(...) NCD_DEBUG_LOG("SVC: " __VA_ARGS__)
#else
#define SVC_DBG(...) ((void)0)
#endif

int state_backend_open_service(NcdStateView **out, NcdStateSourceInfo *info) {
    if (!out) {
        set_error("Invalid output pointer");
        return -1;
    }
    
    *out = NULL;
    memset(g_last_error, 0, sizeof(g_last_error));
    
    SVC_DBG("Initializing platform subsystems...\n");
    
    /* Initialize platform subsystems */
    if (shm_platform_init() != SHM_OK) {
        set_error("Failed to initialize shared memory platform");
        SVC_DBG("Failed to initialize SHM platform\n");
        return -1;
    }
    
    if (ipc_client_init() != NCD_IPC_OK) {
        shm_platform_cleanup();
        set_error("Failed to initialize IPC client");
        SVC_DBG("Failed to initialize IPC client\n");
        return -1;
    }
    
    /* Check if service exists */
    if (!ipc_service_exists()) {
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Service not running");
        SVC_DBG("Service not running\n");
        return -1;
    }
    
    SVC_DBG("Service exists, connecting...\n");
    
    /* Allocate view structure */
    NcdStateView *view = (NcdStateView *)calloc(1, sizeof(NcdStateView));
    if (!view) {
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Failed to allocate state view");
        return -1;
    }
    
    /* Connect to service with retry */
    SVC_DBG("Connecting to service...\n");
    if (!connect_with_retry((NcdIpcClient **)&SERVICE(view).ipc_client, 3, 100)) {
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        /* set_error already called by connect_with_retry */
        SVC_DBG("Failed to connect to service\n");
        return -1;
    }
    SVC_DBG("Connected to service\n");
    
    /* Wait for service to be ready (not STARTING or LOADING) */
    SVC_DBG("Waiting for service to be ready...\n");
    if (!wait_for_service_ready(SERVICE(view).ipc_client, 5000)) {
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        /* set_error already called by wait_for_service_ready */
        SVC_DBG("Service not ready\n");
        return -1;
    }
    SVC_DBG("Service is ready\n");
    
    /* Get state info to find shared memory names */
    SVC_DBG("Getting state info...\n");
    NcdIpcStateInfo state_info;
    NcdIpcResult result = ipc_client_get_state_info(SERVICE(view).ipc_client, &state_info);
    if (result != NCD_IPC_OK) {
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(ipc_error_string(result));
        SVC_DBG("Failed to get state info: %s\n", ipc_error_string(result));
        return -1;
    }
    SVC_DBG("Got state info: meta_size=%u db_size=%u\n", state_info.meta_size, state_info.db_size);
    
    /* Check version compatibility */
    SVC_DBG("Checking version compatibility...\n");
    NcdIpcVersionCheckResult ver_result;
    result = ipc_client_check_version(SERVICE(view).ipc_client, NCD_BUILD_VER, NCD_BUILD_STAMP, &ver_result);
    if (result != NCD_IPC_OK) {
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(ipc_error_string(result));
        SVC_DBG("Version check failed: %s\n", ipc_error_string(result));
        return -1;
    }
    
    if (!ver_result.versions_match) {
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Version mismatch with service");
        SVC_DBG("Version mismatch\n");
        return -1;
    }
    SVC_DBG("Versions match\n");
    
    /* Open and map metadata shared memory */
    SVC_DBG("Opening metadata shared memory...\n");
    char meta_name[256];
    if (!shm_make_meta_name(meta_name, sizeof(meta_name))) {
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Failed to generate metadata SHM name");
        SVC_DBG("Failed to generate metadata SHM name\n");
        return -1;
    }
    
    SVC_DBG("Metadata SHM name: %s\n", meta_name);
    ShmResult shm_result = shm_open_existing(meta_name, SHM_ACCESS_READ, (ShmHandle **)&SERVICE(view).meta_shm);
    if (shm_result != SHM_OK) {
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        SVC_DBG("Failed to open metadata SHM: %s\n", shm_error_string(shm_result));
        return -1;
    }
    SVC_DBG("Metadata SHM opened\n");
    
    shm_result = shm_map(SERVICE(view).meta_shm, SHM_ACCESS_READ, &SERVICE(view).meta_addr, &SERVICE(view).meta_size);
    if (shm_result != SHM_OK) {
        shm_close(SERVICE(view).meta_shm);
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        return -1;
    }
    
    /* Open and map database shared memory */
    SVC_DBG("Opening database shared memory...\n");
    char db_name[256];
    if (!shm_make_db_name(db_name, sizeof(db_name))) {
        shm_unmap(SERVICE(view).meta_addr, SERVICE(view).meta_size);
        shm_close(SERVICE(view).meta_shm);
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Failed to generate database SHM name");
        SVC_DBG("Failed to generate database SHM name\n");
        return -1;
    }
    
    SVC_DBG("Database SHM name: %s\n", db_name);
    shm_result = shm_open_existing(db_name, SHM_ACCESS_READ, (ShmHandle **)&SERVICE(view).db_shm);
    if (shm_result != SHM_OK) {
        shm_unmap(SERVICE(view).meta_addr, SERVICE(view).meta_size);
        shm_close(SERVICE(view).meta_shm);
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        SVC_DBG("Failed to open database SHM: %s\n", shm_error_string(shm_result));
        return -1;
    }
    SVC_DBG("Database SHM opened\n");
    
    shm_result = shm_map(SERVICE(view).db_shm, SHM_ACCESS_READ, &SERVICE(view).db_addr, &SERVICE(view).db_size);
    if (shm_result != SHM_OK) {
        shm_close(SERVICE(view).db_shm);
        shm_unmap(SERVICE(view).meta_addr, SERVICE(view).meta_size);
        shm_close(SERVICE(view).meta_shm);
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        return -1;
    }
    
    /* Load metadata from snapshot */
    SVC_DBG("Loading metadata from snapshot...\n");
    if (!load_metadata_from_snapshot(view)) {
        /* set_error already called */
        SVC_DBG("Failed to load metadata from snapshot: %s\n", g_last_error);
        shm_unmap(SERVICE(view).db_addr, SERVICE(view).db_size);
        shm_close(SERVICE(view).db_shm);
        shm_unmap(SERVICE(view).meta_addr, SERVICE(view).meta_size);
        shm_close(SERVICE(view).meta_shm);
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        return -1;
    }
    SVC_DBG("Metadata loaded successfully\n");
    
    /* Load database from snapshot */
    SVC_DBG("Loading database from snapshot...\n");
    if (!load_database_from_snapshot(view)) {
        /* set_error already called - fall back to metadata-only mode */
        SVC_DBG("Failed to load database from snapshot: %s\n", g_last_error);
        /* For now, we still return error, but future enhancement could allow
         * metadata-only operation for agent commands */
        shm_unmap(SERVICE(view).db_addr, SERVICE(view).db_size);
        shm_close(SERVICE(view).db_shm);
        SERVICE(view).db_addr = NULL;
        SERVICE(view).db_shm = NULL;
        
        /* ZERO-COPY: No metadata copies to cleanup */
        if (SERVICE(view).database_view) {
            if (SERVICE(view).database_view->drives) {
                free(SERVICE(view).database_view->drives);
            }
            free(SERVICE(view).database_view);
            SERVICE(view).database_view = NULL;
        }
        
        shm_unmap(SERVICE(view).meta_addr, SERVICE(view).meta_size);
        shm_close(SERVICE(view).meta_shm);
        ipc_client_disconnect(SERVICE(view).ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        return -1;
    }
    SVC_DBG("Database loaded successfully\n");
    
    /* Populate info structure */
    if (info) {
        info->from_service = true;
        /* Get generation numbers from shared memory headers */
        const ShmSnapshotHdr *meta_hdr = (const ShmSnapshotHdr *)SERVICE(view).meta_addr;
        const ShmSnapshotHdr *db_hdr = (const ShmSnapshotHdr *)SERVICE(view).db_addr;
        info->generation = meta_hdr->generation;
        info->db_generation = db_hdr->generation;
    }
    
    view->info.from_service = true;
    view->info.generation = ((const ShmSnapshotHdr *)SERVICE(view).meta_addr)->generation;
    view->info.db_generation = ((const ShmSnapshotHdr *)SERVICE(view).db_addr)->generation;
    
    SVC_DBG("State backend initialized successfully (from_service=true)\n");
    
    *out = view;
    return 0;
}

/*
 * Connect to service with retry logic.
 * Service might be starting up during connection attempt.
 */
static bool connect_with_retry(NcdIpcClient **client, int max_retries, int retry_delay_ms) {
    for (int i = 0; i < max_retries; i++) {
        *client = ipc_client_connect();
        if (*client != NULL) {
            return true;
        }
        
        if (i < max_retries - 1) {
            platform_sleep_ms(retry_delay_ms);
            /* Exponential backoff */
            retry_delay_ms *= 2;
        } else {
            set_error("Failed to connect to service");
        }
    }
    return false;
}

/*
 * Wait for service to be ready by pinging it.
 * Returns false if timeout or error.
 */
static bool wait_for_service_ready(NcdIpcClient *client, int timeout_ms) {
    int waited = 0;
    int check_interval = 100;  /* Check every 100ms */
    
    while (waited < timeout_ms) {
        NcdIpcResult result = ipc_client_ping(client);
        
        if (result == NCD_IPC_OK) {
            /* Service is ready */
            return true;
        }
        
        if (result == NCD_IPC_ERROR_BUSY_LOADING || 
            result == NCD_IPC_ERROR_BUSY_SCANNING ||
            result == NCD_IPC_ERROR_NOT_READY) {
            /* Service is starting/loading/scanning - wait and retry */
            platform_sleep_ms(check_interval);
            waited += check_interval;
            continue;
        }
        
        /* Other error */
        set_error(ipc_error_string(result));
        return false;
    }
    
    set_error("Timeout waiting for service to be ready");
    return false;
}

/* This is the function called by state_backend_open_best_effort */
int state_backend_try_service(NcdStateView **out, NcdStateSourceInfo *info) {
    return state_backend_open_service(out, info);
}

void state_backend_close_service(NcdStateView *view) {
    if (!view) {
        return;
    }
    
    /* 
     * ZERO-COPY: No metadata copies to free!
     * The metadata and database data are in shared memory, not heap.
     */
    
    /* Free the lightweight database_view structure (but not the data it points to) */
    if (SERVICE(view).database_view) {
        /* 
         * Note: database_view->drives array is heap allocated, but the
         * dirs and name_pool pointers point into shared memory.
         * We free the drives array but NOT the data it references.
         */
        if (SERVICE(view).database_view->drives) {
            free(SERVICE(view).database_view->drives);
        }
        free(SERVICE(view).database_view);
    }
    
    /* Unmap shared memory */
    if (SERVICE(view).meta_addr) {
        shm_unmap(SERVICE(view).meta_addr, SERVICE(view).meta_size);
    }
    if (SERVICE(view).db_addr) {
        shm_unmap(SERVICE(view).db_addr, SERVICE(view).db_size);
    }
    
    /* Close shared memory handles */
    if (SERVICE(view).meta_shm) {
        shm_close(SERVICE(view).meta_shm);
    }
    if (SERVICE(view).db_shm) {
        shm_close(SERVICE(view).db_shm);
    }
    
    /* Disconnect IPC */
    if (SERVICE(view).ipc_client) {
        ipc_client_disconnect(SERVICE(view).ipc_client);
    }
    
    free(view);
    
    /* Cleanup subsystems */
    shm_platform_cleanup();
    ipc_client_cleanup();
}

/* --------------------------------------------------------- state access       */

/*
 * ZERO-COPY accessors.
 * 
 * These return pointers directly into mapped shared memory.
 * The data is read-only and valid until state_backend_close() is called.
 */

const NcdMetadata *state_view_metadata_service(const NcdStateView *view) {
    if (!view || !view->info.from_service || !SERVICE(view).has_metadata) {
        return NULL;
    }
    /* 
     * TODO: For full zero-copy, we need to build a lightweight NcdMetadata
     * that references shared memory sections via offsets.
     * For now, return NULL to indicate service mode requires special handling.
     */
    return SERVICE(view).metadata_view;
}

const NcdDatabase *state_view_database_service(const NcdStateView *view) {
    if (!view || !view->info.from_service || !SERVICE(view).has_database) {
        return NULL;
    }
    return SERVICE(view).database_view;
}

/* --------------------------------------------------------- mutations          */

/* Default retries for busy states */
#define DEFAULT_BUSY_RETRIES 10  /* 10 * 100ms = 1 second max wait */
#define BUSY_RETRY_DELAY_MS 100

/*
 * Helper: Get max retries from view metadata or use default
 * 
 * ZERO-COPY: Reads config directly from shared memory.
 */
static int get_max_retries(const NcdStateView *view) {
    if (!view || !SERVICE(view).has_metadata || !SERVICE(view).metadata_ptr) {
        return DEFAULT_BUSY_RETRIES;
    }
    
    /* Access config section directly from shared memory */
    const ShmMetadataHeader *hdr = (const ShmMetadataHeader *)SERVICE(view).metadata_ptr;
    const ShmSectionDesc *sections = (const ShmSectionDesc *)((uint8_t *)SERVICE(view).metadata_ptr + sizeof(ShmMetadataHeader));
    
    /* Find config section */
    for (uint32_t i = 0; i < hdr->section_count; i++) {
        if (sections[i].type == NCD_SHM_SECTION_CONFIG) {
            const ShmConfigSection *cfg = (const ShmConfigSection *)((uint8_t *)SERVICE(view).metadata_ptr + sections[i].offset);
            uint8_t retry_count = cfg->service_retry_count;
            if (retry_count == 0) {
                return DEFAULT_BUSY_RETRIES;
            }
            return retry_count;
        }
    }
    
    return DEFAULT_BUSY_RETRIES;
}

/*
 * Helper: Retry on BUSY_LOADING or BUSY_SCANNING
 */
static NcdIpcResult retry_on_busy(NcdIpcResult result, int *retries, int max_retries, const char *status_msg) {
    if ((result == NCD_IPC_ERROR_BUSY_LOADING || result == NCD_IPC_ERROR_BUSY_SCANNING) 
        && *retries < max_retries) {
        (*retries)++;
        printf("NCD: Service busy (%s), retrying... (%d/%d)\n", 
               status_msg ? status_msg : "loading", *retries, max_retries);
        platform_sleep_ms(BUSY_RETRY_DELAY_MS);
        return NCD_IPC_ERROR_BUSY;  /* Signal caller to retry */
    }
    return result;
}

int state_backend_submit_heuristic_update_service(NcdStateView *view,
                                                   const char *search,
                                                   const char *target) {
    if (!view || !SERVICE(view).ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_submit_heuristic(SERVICE(view).ipc_client, search, target);
        result = retry_on_busy(result, &retries, max_retries, "loading");
    } while (result == NCD_IPC_ERROR_BUSY);
    
    if (result != NCD_IPC_OK) {
        set_error(ipc_error_string(result));
        return -1;
    }
    
    return 0;
}

int state_backend_submit_metadata_update_service(NcdStateView *view,
                                                  int update_type,
                                                  const void *data,
                                                  size_t data_size) {
    if (!view || !SERVICE(view).ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_submit_metadata(SERVICE(view).ipc_client, 
                                            update_type, data, data_size);
        result = retry_on_busy(result, &retries, max_retries, "loading");
    } while (result == NCD_IPC_ERROR_BUSY);
    
    if (result != NCD_IPC_OK) {
        set_error(ipc_error_string(result));
        return -1;
    }
    
    return 0;
}

int state_backend_request_rescan_service(NcdStateView *view,
                                          const bool drive_mask[26],
                                          bool scan_root_only) {
    if (!view || !SERVICE(view).ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_request_rescan(SERVICE(view).ipc_client, drive_mask, scan_root_only);
        result = retry_on_busy(result, &retries, max_retries, "scanning");
    } while (result == NCD_IPC_ERROR_BUSY);
    
    if (result != NCD_IPC_OK) {
        set_error(ipc_error_string(result));
        return -1;
    }
    
    return 0;
}

int state_backend_request_flush_service(NcdStateView *view) {
    if (!view || !SERVICE(view).ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_request_flush(SERVICE(view).ipc_client);
        result = retry_on_busy(result, &retries, max_retries, "loading");
    } while (result == NCD_IPC_ERROR_BUSY);
    
    if (result != NCD_IPC_OK) {
        set_error(ipc_error_string(result));
        return -1;
    }
    
    return 0;
}

/* --------------------------------------------------------- utilities          */

void state_backend_get_source_info_service(const NcdStateView *view,
                                            NcdStateSourceInfo *info) {
    if (!info) {
        return;
    }
    if (view) {
        *info = view->info;
    } else {
        memset(info, 0, sizeof(*info));
    }
}

/* Service detection */
bool state_backend_service_available(void) {
    return ipc_service_exists();
}
