/*
 * state_backend_service.c  --  Service-backed state backend implementation
 *
 * This implements the state_backend interface by connecting to the
 * NCD state service and mapping shared memory snapshots.
 */

#include "state_backend.h"
#include "shared_state.h"
#include "shm_platform.h"
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

/* Service-backed state view */
struct NcdStateView {
    NcdStateSourceInfo info;
    
    /* IPC client */
    NcdIpcClient *ipc_client;
    
    /* Shared memory */
    ShmHandle *meta_shm;
    ShmHandle *db_shm;
    void      *meta_addr;
    void      *db_addr;
    size_t     meta_size;
    size_t     db_size;
    
    /* In-process views constructed from shared memory */
    NcdMetadata  metadata_copy;   /* Copied from shared memory */
    NcdDatabase  database_view;   /* Points into shared memory */
    bool         has_metadata;
    bool         has_database;
};

/* --------------------------------------------------------- error handling     */

static void set_error(const char *msg) {
    strncpy(g_last_error, msg, sizeof(g_last_error) - 1);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
}

/* --------------------------------------------------------- snapshot loading   */

static bool load_metadata_from_snapshot(NcdStateView *view) {
    if (!view->meta_addr) {
        set_error("Metadata not mapped");
        return false;
    }
    
    /* Validate header */
    if (!shm_validate_header(view->meta_addr, view->meta_size, NCD_SHM_META_MAGIC)) {
        set_error("Invalid metadata snapshot header");
        return false;
    }
    
    /* Validate checksum */
    if (!shm_validate_checksum(view->meta_addr, view->meta_size)) {
        set_error("Metadata snapshot checksum mismatch");
        return false;
    }
    
    const ShmSnapshotHdr *hdr = (const ShmSnapshotHdr *)view->meta_addr;
    
    /* Copy metadata structure from snapshot */
    memset(&view->metadata_copy, 0, sizeof(view->metadata_copy));
    
    /* Find config section */
    const ShmSectionDesc *cfg_desc = shm_find_section(hdr, NCD_SHM_SECTION_CONFIG);
    if (cfg_desc) {
        const ShmConfigSection *cfg = (const ShmConfigSection *)
            shm_get_section_ptr(view->meta_addr, cfg_desc);
        
        view->metadata_copy.cfg.magic = cfg->magic;
        view->metadata_copy.cfg.version = cfg->version;
        view->metadata_copy.cfg.default_show_hidden = cfg->default_show_hidden;
        view->metadata_copy.cfg.default_show_system = cfg->default_show_system;
        view->metadata_copy.cfg.default_fuzzy_match = cfg->default_fuzzy_match;
        view->metadata_copy.cfg.default_timeout = cfg->default_timeout;
        view->metadata_copy.cfg.has_defaults = cfg->has_defaults;
    }
    
    /* Find groups section */
    const ShmSectionDesc *groups_desc = shm_find_section(hdr, NCD_SHM_SECTION_GROUPS);
    if (groups_desc && groups_desc->size > 0) {
        const ShmGroupsSection *groups = (const ShmGroupsSection *)
            shm_get_section_ptr(view->meta_addr, groups_desc);
        
        view->metadata_copy.groups.groups = (NcdGroupEntry *)
            calloc(groups->entry_count, sizeof(NcdGroupEntry));
        if (view->metadata_copy.groups.groups) {
            view->metadata_copy.groups.count = (int)groups->entry_count;
            view->metadata_copy.groups.capacity = (int)groups->entry_count;
            
            const ShmGroupEntry *entries = (const ShmGroupEntry *)
                ((const uint8_t *)view->meta_addr + groups->entries_off);
            
            for (uint32_t i = 0; i < groups->entry_count; i++) {
                const char *name = (const char *)view->meta_addr + groups->pool_off + entries[i].name_off;
                const char *path = (const char *)view->meta_addr + groups->pool_off + entries[i].path_off;
                
                strncpy(view->metadata_copy.groups.groups[i].name, name, NCD_MAX_GROUP_NAME - 1);
                strncpy(view->metadata_copy.groups.groups[i].path, path, NCD_MAX_PATH - 1);
                view->metadata_copy.groups.groups[i].created = entries[i].created;
            }
        }
    }
    
    /* Find heuristics section */
    const ShmSectionDesc *heur_desc = shm_find_section(hdr, NCD_SHM_SECTION_HEURISTICS);
    if (heur_desc && heur_desc->size > 0) {
        const ShmHeuristicsSection *heur = (const ShmHeuristicsSection *)
            shm_get_section_ptr(view->meta_addr, heur_desc);
        
        view->metadata_copy.heuristics.entries = (NcdHeurEntryV2 *)
            calloc(heur->entry_count, sizeof(NcdHeurEntryV2));
        if (view->metadata_copy.heuristics.entries) {
            view->metadata_copy.heuristics.count = (int)heur->entry_count;
            view->metadata_copy.heuristics.capacity = (int)heur->entry_count;
            
            const ShmHeurEntry *entries = (const ShmHeurEntry *)
                ((const uint8_t *)view->meta_addr + heur->entries_off);
            
            for (uint32_t i = 0; i < heur->entry_count; i++) {
                const char *search = (const char *)view->meta_addr + heur->pool_off + entries[i].search_off;
                const char *target = (const char *)view->meta_addr + heur->pool_off + entries[i].target_off;
                
                strncpy(view->metadata_copy.heuristics.entries[i].search, search, NCD_MAX_PATH - 1);
                strncpy(view->metadata_copy.heuristics.entries[i].target, target, NCD_MAX_PATH - 1);
                view->metadata_copy.heuristics.entries[i].frequency = entries[i].frequency;
                view->metadata_copy.heuristics.entries[i].last_used = entries[i].last_used * 86400;  /* Convert days to seconds */
            }
        }
    }
    
    /* Find exclusions section */
    const ShmSectionDesc *excl_desc = shm_find_section(hdr, NCD_SHM_SECTION_EXCLUSIONS);
    if (excl_desc && excl_desc->size > 0) {
        const ShmExclusionsSection *excl = (const ShmExclusionsSection *)
            shm_get_section_ptr(view->meta_addr, excl_desc);
        
        view->metadata_copy.exclusions.entries = (NcdExclusionEntry *)
            calloc(excl->entry_count, sizeof(NcdExclusionEntry));
        if (view->metadata_copy.exclusions.entries) {
            view->metadata_copy.exclusions.count = (int)excl->entry_count;
            view->metadata_copy.exclusions.capacity = (int)excl->entry_count;
            
            const ShmExclusionEntry *entries = (const ShmExclusionEntry *)
                ((const uint8_t *)view->meta_addr + excl->entries_off);
            
            for (uint32_t i = 0; i < excl->entry_count; i++) {
                const char *pattern = (const char *)view->meta_addr + excl->pool_off + entries[i].pattern_off;
                
                strncpy(view->metadata_copy.exclusions.entries[i].pattern, pattern, NCD_MAX_PATH - 1);
                view->metadata_copy.exclusions.entries[i].drive = entries[i].drive;
                view->metadata_copy.exclusions.entries[i].match_from_root = entries[i].match_from_root;
                view->metadata_copy.exclusions.entries[i].has_parent_match = entries[i].has_parent_match;
            }
        }
    }
    
    /* Find dir history section */
    const ShmSectionDesc *dh_desc = shm_find_section(hdr, NCD_SHM_SECTION_DIR_HISTORY);
    if (dh_desc && dh_desc->size > 0) {
        const ShmDirHistorySection *dh = (const ShmDirHistorySection *)
            shm_get_section_ptr(view->meta_addr, dh_desc);
        
        view->metadata_copy.dir_history.count = (int)dh->entry_count;
        
        const ShmDirHistoryEntry *entries = (const ShmDirHistoryEntry *)
            ((const uint8_t *)view->meta_addr + dh->entries_off);
        
        for (uint32_t i = 0; i < dh->entry_count && i < NCD_DIR_HISTORY_MAX; i++) {
            const char *path = (const char *)view->meta_addr + dh->pool_off + entries[i].path_off;
            
            strncpy(view->metadata_copy.dir_history.entries[i].path, path, NCD_MAX_PATH - 1);
            view->metadata_copy.dir_history.entries[i].drive = entries[i].drive;
            view->metadata_copy.dir_history.entries[i].timestamp = entries[i].timestamp;
        }
    }
    
    view->has_metadata = true;
    return true;
}

static bool load_database_from_snapshot(NcdStateView *view) {
    if (!view->db_addr) {
        set_error("Database not mapped");
        return false;
    }
    
    /* Validate header */
    if (!shm_validate_header(view->db_addr, view->db_size, NCD_SHM_DB_MAGIC)) {
        set_error("Invalid database snapshot header");
        return false;
    }
    
    /* Validate checksum */
    if (!shm_validate_checksum(view->db_addr, view->db_size)) {
        set_error("Database snapshot checksum mismatch");
        return false;
    }
    
    const ShmSnapshotHdr *hdr = (const ShmSnapshotHdr *)view->db_addr;
    
    /* Set up database view to point into shared memory */
    memset(&view->database_view, 0, sizeof(view->database_view));
    
    view->database_view.version = 2;  /* Current version */
    view->database_view.last_scan = (time_t)hdr->generation;  /* Use generation as proxy */
    
    /* Locate database section header (after section descriptors) */
    size_t section_table_size = hdr->section_count * sizeof(ShmSectionDesc);
    size_t dbsec_offset = sizeof(ShmSnapshotHdr) + section_table_size;
    
    /* Align to 8 bytes */
    dbsec_offset = (dbsec_offset + 7) & ~7;
    
    if (dbsec_offset + sizeof(ShmDatabaseSection) > view->db_size) {
        set_error("Database section header out of bounds");
        return false;
    }
    
    const ShmDatabaseSection *dbsec = (const ShmDatabaseSection *)((uint8_t *)view->db_addr + dbsec_offset);
    
    view->database_view.default_show_hidden = dbsec->show_hidden;
    view->database_view.default_show_system = dbsec->show_system;
    view->database_view.drive_count = (int)dbsec->drive_count;
    
    if (dbsec->drive_count == 0) {
        /* Empty database - valid but has no drives */
        view->has_database = true;
        return true;
    }
    
    if (dbsec->drive_count > NCD_SHM_MAX_DRIVES) {
        set_error("Too many drives in snapshot");
        return false;
    }
    
    /* Allocate DriveData array */
    view->database_view.drives = (DriveData *)ncd_malloc_array(dbsec->drive_count, sizeof(DriveData));
    if (!view->database_view.drives) {
        set_error("Failed to allocate drive array");
        return false;
    }
    
    view->database_view.drive_capacity = (int)dbsec->drive_count;
    
    /* Drive sections array follows database section header */
    size_t drive_secs_offset = dbsec_offset + sizeof(ShmDatabaseSection);
    if (drive_secs_offset + dbsec->drive_count * sizeof(ShmDriveSection) > view->db_size) {
        set_error("Drive sections out of bounds");
        return false;
    }
    
    const ShmDriveSection *drive_secs = (const ShmDriveSection *)((uint8_t *)view->db_addr + drive_secs_offset);
    
    /* Calculate base offset for directory entries and name pools */
    uint32_t entries_base = (uint32_t)(drive_secs_offset + dbsec->drive_count * sizeof(ShmDriveSection));
    
    /* Reconstruct each drive */
    for (uint32_t i = 0; i < dbsec->drive_count; i++) {
        DriveData *drv = &view->database_view.drives[i];
        const ShmDriveSection *shmdrv = &drive_secs[i];
        
        drv->letter = shmdrv->letter;
        memcpy(drv->label, shmdrv->label, sizeof(drv->label));
        drv->type = (UINT)shmdrv->type;
        drv->dir_count = (int)shmdrv->dir_count;
        drv->dir_capacity = (int)shmdrv->dir_count;
        
        /* Calculate this drive's data location */
        uint32_t drive_entries_start = entries_base;
        for (uint32_t k = 0; k < i; k++) {
            drive_entries_start += drive_secs[k].dir_count * sizeof(ShmDirEntry) + drive_secs[k].pool_size;
        }
        
        if (shmdrv->dir_count > 0) {
            /* Directory entries array */
            uint32_t dirs_offset = drive_entries_start;
            uint32_t pool_offset = drive_entries_start + shmdrv->dir_count * sizeof(ShmDirEntry);
            
            if (pool_offset + shmdrv->pool_size > view->db_size) {
                set_error("Drive data out of bounds");
                /* Cleanup already allocated drives would happen in close */
                return false;
            }
            
            /* Point directly into shared memory - no copy needed */
            drv->dirs = (DirEntry *)((uint8_t *)view->db_addr + dirs_offset);
            drv->name_pool = (char *)((uint8_t *)view->db_addr + pool_offset);
            drv->name_pool_len = shmdrv->pool_size;
            drv->name_pool_cap = shmdrv->pool_size;
        } else {
            drv->dirs = NULL;
            drv->name_pool = NULL;
            drv->name_pool_len = 0;
            drv->name_pool_cap = 0;
        }
    }
    
    /* Mark as blob-style so free logic knows not to free individual pools */
    view->database_view.is_blob = true;
    view->database_view.blob_buf = view->db_addr;  /* Reference to shared memory */
    
    view->has_database = true;
    return true;
}

/* --------------------------------------------------------- lifecycle          */

/* Forward declarations for helper functions */
static bool connect_with_retry(NcdIpcClient **client, int max_retries, int retry_delay_ms);
static bool wait_for_service_ready(NcdIpcClient *client, int timeout_ms);

int state_backend_open_service(NcdStateView **out, NcdStateSourceInfo *info) {
    if (!out) {
        set_error("Invalid output pointer");
        return -1;
    }
    
    *out = NULL;
    memset(g_last_error, 0, sizeof(g_last_error));
    
    /* Initialize platform subsystems */
    if (shm_platform_init() != SHM_OK) {
        set_error("Failed to initialize shared memory platform");
        return -1;
    }
    
    if (ipc_client_init() != NCD_IPC_OK) {
        shm_platform_cleanup();
        set_error("Failed to initialize IPC client");
        return -1;
    }
    
    /* Check if service exists */
    if (!ipc_service_exists()) {
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Service not running");
        return -1;
    }
    
    /* Allocate view structure */
    NcdStateView *view = (NcdStateView *)calloc(1, sizeof(NcdStateView));
    if (!view) {
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Failed to allocate state view");
        return -1;
    }
    
    /* Connect to service with retry */
    if (!connect_with_retry(&view->ipc_client, 3, 100)) {
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        /* set_error already called by connect_with_retry */
        return -1;
    }
    
    /* Wait for service to be ready (not STARTING or LOADING) */
    if (!wait_for_service_ready(view->ipc_client, 5000)) {
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        /* set_error already called by wait_for_service_ready */
        return -1;
    }
    
    /* Get state info to find shared memory names */
    NcdIpcStateInfo state_info;
    NcdIpcResult result = ipc_client_get_state_info(view->ipc_client, &state_info);
    if (result != NCD_IPC_OK) {
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(ipc_error_string(result));
        return -1;
    }
    
    /* Check version compatibility */
    NcdIpcVersionCheckResult ver_result;
    result = ipc_client_check_version(view->ipc_client, NCD_BUILD_VER, NCD_BUILD_STAMP, &ver_result);
    if (result != NCD_IPC_OK) {
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(ipc_error_string(result));
        return -1;
    }
    
    if (!ver_result.versions_match) {
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Version mismatch with service");
        return -1;
    }
    
    /* Open and map metadata shared memory */
    char meta_name[256];
    if (!shm_make_meta_name(meta_name, sizeof(meta_name))) {
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Failed to generate metadata SHM name");
        return -1;
    }
    
    ShmResult shm_result = shm_open_existing(meta_name, SHM_ACCESS_READ, &view->meta_shm);
    if (shm_result != SHM_OK) {
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        return -1;
    }
    
    shm_result = shm_map(view->meta_shm, SHM_ACCESS_READ, &view->meta_addr, &view->meta_size);
    if (shm_result != SHM_OK) {
        shm_close(view->meta_shm);
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        return -1;
    }
    
    /* Open and map database shared memory */
    char db_name[256];
    if (!shm_make_db_name(db_name, sizeof(db_name))) {
        shm_unmap(view->meta_addr, view->meta_size);
        shm_close(view->meta_shm);
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Failed to generate database SHM name");
        return -1;
    }
    
    shm_result = shm_open_existing(db_name, SHM_ACCESS_READ, &view->db_shm);
    if (shm_result != SHM_OK) {
        shm_unmap(view->meta_addr, view->meta_size);
        shm_close(view->meta_shm);
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        return -1;
    }
    
    shm_result = shm_map(view->db_shm, SHM_ACCESS_READ, &view->db_addr, &view->db_size);
    if (shm_result != SHM_OK) {
        shm_close(view->db_shm);
        shm_unmap(view->meta_addr, view->meta_size);
        shm_close(view->meta_shm);
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(shm_error_string(shm_result));
        return -1;
    }
    
    /* Load metadata from snapshot */
    if (!load_metadata_from_snapshot(view)) {
        /* set_error already called */
        shm_unmap(view->db_addr, view->db_size);
        shm_close(view->db_shm);
        shm_unmap(view->meta_addr, view->meta_size);
        shm_close(view->meta_shm);
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        return -1;
    }
    
    /* Load database from snapshot */
    if (!load_database_from_snapshot(view)) {
        /* set_error already called - fall back to metadata-only mode */
        /* For now, we still return error, but future enhancement could allow
         * metadata-only operation for agent commands */
        shm_unmap(view->db_addr, view->db_size);
        shm_close(view->db_shm);
        view->db_addr = NULL;
        view->db_shm = NULL;
        
        /* Cleanup metadata copy since we're failing */
        if (view->metadata_copy.groups.groups) {
            free(view->metadata_copy.groups.groups);
        }
        if (view->metadata_copy.heuristics.entries) {
            free(view->metadata_copy.heuristics.entries);
        }
        if (view->metadata_copy.exclusions.entries) {
            free(view->metadata_copy.exclusions.entries);
        }
        
        shm_unmap(view->meta_addr, view->meta_size);
        shm_close(view->meta_shm);
        ipc_client_disconnect(view->ipc_client);
        free(view);
        shm_platform_cleanup();
        ipc_client_cleanup();
        return -1;
    }
    
    /* Populate info structure */
    if (info) {
        info->from_service = true;
        /* Get generation numbers from shared memory headers */
        const ShmSnapshotHdr *meta_hdr = (const ShmSnapshotHdr *)view->meta_addr;
        const ShmSnapshotHdr *db_hdr = (const ShmSnapshotHdr *)view->db_addr;
        info->generation = meta_hdr->generation;
        info->db_generation = db_hdr->generation;
    }
    
    view->info.from_service = true;
    view->info.generation = ((const ShmSnapshotHdr *)view->meta_addr)->generation;
    view->info.db_generation = ((const ShmSnapshotHdr *)view->db_addr)->generation;
    
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
    
    /* Free copied metadata */
    if (view->metadata_copy.groups.groups) {
        free(view->metadata_copy.groups.groups);
    }
    if (view->metadata_copy.heuristics.entries) {
        free(view->metadata_copy.heuristics.entries);
    }
    if (view->metadata_copy.exclusions.entries) {
        free(view->metadata_copy.exclusions.entries);
    }
    
    /* Unmap shared memory */
    if (view->meta_addr) {
        shm_unmap(view->meta_addr, view->meta_size);
    }
    if (view->db_addr) {
        shm_unmap(view->db_addr, view->db_size);
    }
    
    /* Close shared memory handles */
    if (view->meta_shm) {
        shm_close(view->meta_shm);
    }
    if (view->db_shm) {
        shm_close(view->db_shm);
    }
    
    /* Disconnect IPC */
    if (view->ipc_client) {
        ipc_client_disconnect(view->ipc_client);
    }
    
    free(view);
    
    /* Cleanup subsystems */
    shm_platform_cleanup();
    ipc_client_cleanup();
}

/* --------------------------------------------------------- state access       */

const NcdMetadata *state_view_metadata_service(const NcdStateView *view) {
    if (!view || !view->has_metadata) {
        return NULL;
    }
    return &view->metadata_copy;
}

const NcdDatabase *state_view_database_service(const NcdStateView *view) {
    if (!view || !view->has_database) {
        return NULL;
    }
    return &view->database_view;
}

/* --------------------------------------------------------- mutations          */

/* Default retries for busy states */
#define DEFAULT_BUSY_RETRIES 10  /* 10 * 100ms = 1 second max wait */
#define BUSY_RETRY_DELAY_MS 100

/*
 * Helper: Get max retries from view metadata or use default
 */
static int get_max_retries(const NcdStateView *view) {
    if (!view || !view->has_metadata) {
        return DEFAULT_BUSY_RETRIES;
    }
    uint8_t retry_count = view->metadata_copy.cfg.service_retry_count;
    if (retry_count == 0) {
        return DEFAULT_BUSY_RETRIES;
    }
    return retry_count;
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
    if (!view || !view->ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_submit_heuristic(view->ipc_client, search, target);
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
    if (!view || !view->ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_submit_metadata(view->ipc_client, 
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
    if (!view || !view->ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_request_rescan(view->ipc_client, drive_mask, scan_root_only);
        result = retry_on_busy(result, &retries, max_retries, "scanning");
    } while (result == NCD_IPC_ERROR_BUSY);
    
    if (result != NCD_IPC_OK) {
        set_error(ipc_error_string(result));
        return -1;
    }
    
    return 0;
}

int state_backend_request_flush_service(NcdStateView *view) {
    if (!view || !view->ipc_client) {
        set_error("Not connected to service");
        return -1;
    }
    
    int retries = 0;
    int max_retries = get_max_retries(view);
    NcdIpcResult result;
    
    do {
        result = ipc_client_request_flush(view->ipc_client);
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
