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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
    
    /* Note: The full implementation would traverse the database section
     * and set up DriveData pointers into the shared memory.
     * For simplicity, this creates a minimal view. */
    
    view->has_database = true;
    return true;
}

/* --------------------------------------------------------- lifecycle          */

int state_backend_open_service(NcdStateView **out, NcdStateSourceInfo *info) {
    if (!out) {
        set_error("Invalid output pointer");
        return -1;
    }
    
    *out = NULL;
    memset(g_last_error, 0, sizeof(g_last_error));
    
    /* Initialize IPC client */
    if (ipc_client_init() != 0) {
        set_error("Failed to initialize IPC");
        return -1;
    }
    
    /* Initialize shared memory platform */
    if (shm_platform_init() != SHM_OK) {
        ipc_client_cleanup();
        set_error("Failed to initialize shared memory");
        return -1;
    }
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Failed to connect to service");
        return -1;
    }
    
    /* Get state info from service */
    NcdIpcStateInfo ipc_info;
    NcdIpcResult result = ipc_client_get_state_info(client, &ipc_info);
    if (result != NCD_IPC_OK) {
        ipc_client_disconnect(client);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error(ipc_error_string(result));
        return -1;
    }
    
    /* Allocate view */
    NcdStateView *view = (NcdStateView *)calloc(1, sizeof(NcdStateView));
    if (!view) {
        ipc_client_disconnect(client);
        shm_platform_cleanup();
        ipc_client_cleanup();
        set_error("Out of memory");
        return -1;
    }
    
    view->ipc_client = client;
    view->info.from_service = true;
    view->info.generation = ipc_info.meta_generation;
    view->info.db_generation = ipc_info.db_generation;
    
    /* Open and map metadata shared memory */
    ShmHandle *meta_shm;
    if (shm_open(ipc_info.meta_name, SHM_ACCESS_READ, &meta_shm) != SHM_OK) {
        set_error("Failed to open metadata shared memory");
        goto error;
    }
    view->meta_shm = meta_shm;
    
    void *meta_addr;
    size_t meta_size;
    if (shm_map(meta_shm, SHM_ACCESS_READ, &meta_addr, &meta_size) != SHM_OK) {
        set_error("Failed to map metadata shared memory");
        goto error;
    }
    view->meta_addr = meta_addr;
    view->meta_size = meta_size;
    
    /* Open and map database shared memory */
    ShmHandle *db_shm;
    if (shm_open(ipc_info.db_name, SHM_ACCESS_READ, &db_shm) != SHM_OK) {
        set_error("Failed to open database shared memory");
        goto error;
    }
    view->db_shm = db_shm;
    
    void *db_addr;
    size_t db_size;
    if (shm_map(db_shm, SHM_ACCESS_READ, &db_addr, &db_size) != SHM_OK) {
        set_error("Failed to map database shared memory");
        goto error;
    }
    view->db_addr = db_addr;
    view->db_size = db_size;
    
    /* Load from snapshots */
    if (!load_metadata_from_snapshot(view)) {
        goto error;
    }
    
    if (!load_database_from_snapshot(view)) {
        goto error;
    }
    
    /* Fill output info */
    if (info) {
        *info = view->info;
    }
    
    *out = view;
    return 0;
    
error:
    state_backend_close(view);
    return -1;
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

/* Maximum retries for busy states */
#define MAX_BUSY_RETRIES 50  /* 50 * 100ms = 5 seconds max wait */
#define BUSY_RETRY_DELAY_MS 100

/*
 * Helper: Retry on BUSY_LOADING or BUSY_SCANNING
 */
static NcdIpcResult retry_on_busy(NcdIpcResult result, int *retries, const char *status_msg) {
    if ((result == NCD_IPC_ERROR_BUSY_LOADING || result == NCD_IPC_ERROR_BUSY_SCANNING) 
        && *retries < MAX_BUSY_RETRIES) {
        (*retries)++;
        printf("NCD: Service busy (%s), retrying... (%d/%d)\n", 
               status_msg ? status_msg : "loading", *retries, MAX_BUSY_RETRIES);
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
    NcdIpcResult result;
    
    do {
        result = ipc_client_submit_heuristic(view->ipc_client, search, target);
        result = retry_on_busy(result, &retries, "loading");
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
    NcdIpcResult result;
    
    do {
        result = ipc_client_submit_metadata(view->ipc_client, 
                                            update_type, data, data_size);
        result = retry_on_busy(result, &retries, "loading");
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
    NcdIpcResult result;
    
    do {
        result = ipc_client_request_rescan(view->ipc_client, drive_mask, scan_root_only);
        result = retry_on_busy(result, &retries, "scanning");
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
    NcdIpcResult result;
    
    do {
        result = ipc_client_request_flush(view->ipc_client);
        result = retry_on_busy(result, &retries, "loading");
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
