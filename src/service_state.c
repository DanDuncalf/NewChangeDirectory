/*
 * service_state.c  --  Service state management implementation
 */

#include "service_state.h"
#include "database.h"
#include "platform.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <limits.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#endif

/* --------------------------------------------------------- internal state     */

/* Pending request node for queue */
typedef struct PendingRequestNode {
    PendingRequestType type;
    void *data;
    size_t data_len;
    struct PendingRequestNode *next;
} PendingRequestNode;

struct ServiceState {
    NcdMetadata *metadata;
    NcdDatabase *database;
    
    uint64_t meta_generation;
    uint64_t db_generation;
    
    uint32_t dirty_flags;
    
    time_t last_flush;
    time_t last_rescan;
    time_t last_db_mutation;  /* Timestamp of last database modification (for deferred flush) */
    
    uint64_t request_count;
    uint64_t mutation_count;
    
    /* Runtime state machine */
    ServiceRuntimeState runtime_state;
    char status_message[256];
    
    /* Request queue */
    PendingRequestNode *pending_head;
    PendingRequestNode *pending_tail;
    int pending_count;
    int pending_max;  /* Maximum queue size */
    
    /* Thread safety: mutex for protecting flush and state changes */
#if NCD_PLATFORM_WINDOWS
    CRITICAL_SECTION state_mutex;
#else
    pthread_mutex_t state_mutex;
#endif
    int mutex_initialized;
    
    /* Shutdown flag: when set, service is shutting down and should reject new operations */
    int shutdown_requested;
};

/* --------------------------------------------------------- mutex helpers      */

static void state_lock_init(ServiceState *state) {
    if (state && !state->mutex_initialized) {
#if NCD_PLATFORM_WINDOWS
        InitializeCriticalSection(&state->state_mutex);
#else
        pthread_mutex_init(&state->state_mutex, NULL);
#endif
        state->mutex_initialized = 1;
    }
}

static void state_lock(ServiceState *state) {
    if (state && state->mutex_initialized) {
#if NCD_PLATFORM_WINDOWS
        EnterCriticalSection(&state->state_mutex);
#else
        pthread_mutex_lock(&state->state_mutex);
#endif
    }
}

static void state_unlock(ServiceState *state) {
    if (state && state->mutex_initialized) {
#if NCD_PLATFORM_WINDOWS
        LeaveCriticalSection(&state->state_mutex);
#else
        pthread_mutex_unlock(&state->state_mutex);
#endif
    }
}

static void state_lock_cleanup(ServiceState *state) {
    if (state && state->mutex_initialized) {
#if NCD_PLATFORM_WINDOWS
        DeleteCriticalSection(&state->state_mutex);
#else
        pthread_mutex_destroy(&state->state_mutex);
#endif
        state->mutex_initialized = 0;
    }
}

/* --------------------------------------------------------- lifecycle          */

ServiceState *service_state_init(void) {
    ServiceState *state = (ServiceState *)calloc(1, sizeof(ServiceState));
    if (!state) {
        return NULL;
    }
    
    /* Initialize mutex first for thread safety */
    state_lock_init(state);
    
    /* Initialize runtime state - database loading is lazy */
    state->runtime_state = SERVICE_STATE_STARTING;
    strncpy(state->status_message, "Initializing...", sizeof(state->status_message) - 1);
    state->status_message[sizeof(state->status_message) - 1] = '\0';
    
    /* Initialize request queue */
    state->pending_head = NULL;
    state->pending_tail = NULL;
    state->pending_count = 0;
    state->pending_max = 1000;  /* Max 1000 pending requests */
    
    /* Load metadata from disk (small, do synchronously) */
    state->metadata = db_metadata_load();
    if (!state->metadata) {
        state->metadata = db_metadata_create();
        if (!state->metadata) {
            state_lock_cleanup(state);
            free(state);
            return NULL;
        }
    }
    
    /* Create empty database (will be populated lazily) */
    state->database = db_create();
    if (!state->database) {
        db_metadata_free(state->metadata);
        state_lock_cleanup(state);
        free(state);
        return NULL;
    }
    
    /* Initialize generations */
    state->meta_generation = 1;
    state->db_generation = 1;
    state->last_flush = time(NULL);
    state->last_rescan = 0;
    state->shutdown_requested = 0;
    
    return state;
}

/*
 * service_state_load_databases  --  Load all drive databases
 *
 * This is called by the background loader thread.
 */
bool service_state_load_databases(ServiceState *state) {
    if (!state) return false;
    
    service_state_set_runtime_state(state, SERVICE_STATE_LOADING);
    service_state_set_status_message(state, "Loading databases...");
    
    /* Load per-drive databases */
    char drives[26];
    int drive_count = platform_get_available_drives(drives, 26);
    NCD_DEBUG_LOG("DEBUG: Loading databases for %d drives\n", drive_count);
    int loaded_count = 0;
    
    for (int i = 0; i < drive_count; i++) {
        char path[MAX_PATH];
        if (!ncd_platform_db_drive_path(drives[i], path, sizeof(path))) {
            NCD_DEBUG_LOG("DEBUG: db_drive_path failed for drive %c:\n", drives[i]);
            continue;
        }
        
        /* Update status with progress */
        char status[256];
        snprintf(status, sizeof(status), "Loading drive %c: (%d/%d)...", 
                 drives[i], i + 1, drive_count);
        service_state_set_status_message(state, status);
        
        /* Check if file exists */
        FILE *f = fopen(path, "rb");
        if (!f) {
            NCD_DEBUG_LOG("DEBUG: Database file not found for drive %c: (%s)\n", drives[i], path);
            continue;
        }
        fclose(f);
        
        /* Load this drive's database */
        NcdDatabase *drive_db = db_load_auto(path);
        if (!drive_db) {
            NCD_DEBUG_LOG("DEBUG: Failed to load database for drive %c:\n", drives[i]);
            continue;
        }
        
        NCD_DEBUG_LOG("DEBUG: Successfully loaded drive %c: database (%d drives, %d dirs)\n", 
                      drives[i], drive_db->drive_count, 
                      drive_db->drive_count > 0 ? drive_db->drives[0].dir_count : 0);
        
        /* Merge into main database */
        for (int d = 0; d < drive_db->drive_count; d++) {
            DriveData *src = &drive_db->drives[d];
            NCD_DEBUG_LOG("DEBUG: Merging drive %c: (type=%d, dirs=%d)\n", 
                          src->letter, src->type, src->dir_count);
            DriveData *dst = db_add_drive(state->database, src->letter);
            if (!dst) {
                NCD_DEBUG_LOG("DEBUG: Failed to add drive %c: to main database\n", src->letter);
                continue;
            }
            
            dst->type = src->type;
            memcpy(dst->label, src->label, sizeof(dst->label));
            
            /* Copy all directories */
            NCD_DEBUG_LOG("DEBUG: Copying %d directories from drive %c:\n", src->dir_count, src->letter);
            for (int dir_idx = 0; dir_idx < src->dir_count; dir_idx++) {
                DirEntry *entry = &src->dirs[dir_idx];
                const char *name = src->name_pool + entry->name_off;
                db_add_dir(dst, name, entry->parent,
                          entry->is_hidden, entry->is_system);
            }
            NCD_DEBUG_LOG("DEBUG: Finished copying directories for drive %c:\n", src->letter);
        }
        
        db_free(drive_db);
        loaded_count++;
        NCD_DEBUG_LOG("DEBUG: Drive %c: loaded successfully (%d/%d)\n", drives[i], loaded_count, drive_count);
    }
    
    /* Filter excluded directories from the loaded database */
    if (state->metadata && state->metadata->exclusions.count > 0) {
        int removed = db_filter_excluded(state->database, state->metadata);
        if (removed > 0) {
            NCD_DEBUG_LOG("DEBUG: Filtered %d excluded dirs from service database\n", removed);
        }
    }
    
    state->last_rescan = state->database->last_scan;
    
    char status[256];
    snprintf(status, sizeof(status), "Loaded %d drives", loaded_count);
    service_state_set_status_message(state, status);
    
    NCD_DEBUG_LOG("DEBUG: service_state_load_databases completed. loaded_count=%d, drive_count=%d\n", 
                  loaded_count, drive_count);
    
    return loaded_count > 0 || drive_count == 0;
}

void service_state_cleanup(ServiceState *state) {
    if (!state) {
        return;
    }
    
    /* Mark shutdown requested to block new operations */
    state->shutdown_requested = 1;
    
    /* Flush any pending changes (mutex protected) */
    if (state->dirty_flags != 0) {
        service_state_flush(state);
    }
    
    /* Free resources */
    if (state->metadata) {
        db_metadata_free(state->metadata);
    }
    
    if (state->database) {
        db_free(state->database);
    }
    
    /* Clean up mutex */
    state_lock_cleanup(state);
    
    free(state);
}

/* --------------------------------------------------------- state access       */

const NcdMetadata *service_state_get_metadata(const ServiceState *state) {
    if (!state) {
        return NULL;
    }
    return state->metadata;
}

const NcdDatabase *service_state_get_database(const ServiceState *state) {
    if (!state) {
        return NULL;
    }
    return state->database;
}

/* --------------------------------------------------------- mutations          */

bool service_state_note_heuristic(ServiceState *state,
                                   const char *search,
                                   const char *target) {
    if (!state || !state->metadata || !search || !target) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    state_lock(state);
    db_heur_note_choice(state->metadata, search, target);
    state->dirty_flags |= DIRTY_HEURISTICS;
    state->mutation_count++;
    state_unlock(state);
    
    return true;
}

bool service_state_add_group(ServiceState *state,
                              const char *name,
                              const char *path) {
    if (!state || !state->metadata || !name || !path) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    bool result = db_group_set(state->metadata, name, path);
    if (result) {
        state_lock(state);
        state->dirty_flags |= DIRTY_GROUPS;
        state->mutation_count++;
        state_unlock(state);
    }
    
    return result;
}

bool service_state_remove_group(ServiceState *state, const char *name) {
    if (!state || !state->metadata || !name) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    bool result = db_group_remove(state->metadata, name);
    if (result) {
        state_lock(state);
        state->dirty_flags |= DIRTY_GROUPS;
        state->mutation_count++;
        state_unlock(state);
    }
    
    return result;
}

bool service_state_add_exclusion(ServiceState *state, const char *pattern) {
    if (!state || !state->metadata || !pattern) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    bool result = db_exclusion_add(state->metadata, pattern);
    if (result) {
        int removed = 0;

        /*
         * Service mode behavior: apply exclusion immediately to in-memory DB
         * so search/agent consumers see consistent results without per-query
         * exclusion filtering.
         */
        if (state->database) {
            removed = db_filter_excluded(state->database, state->metadata);
            if (removed > 0) {
                NCD_DEBUG_LOG("DEBUG: service exclusion add pruned %d directories\n", removed);
            }
        }

        state_lock(state);
        state->dirty_flags |= DIRTY_EXCLUSIONS;
        if (removed > 0) {
            state->dirty_flags |= DIRTY_DATABASE_PARTIAL;
            state->last_db_mutation = time(NULL);
        }
        state->mutation_count++;
        state_unlock(state);
    }
    
    return result;
}

bool service_state_remove_exclusion(ServiceState *state, const char *pattern) {
    if (!state || !state->metadata || !pattern) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    /* Find and remove by pattern */
    bool found = false;
    NcdExclusionList *list = &state->metadata->exclusions;
    
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->entries[i].pattern, pattern) == 0) {
            /* Shift remaining entries */
            memmove(&list->entries[i],
                    &list->entries[i + 1],
                    (list->count - i - 1) * sizeof(NcdExclusionEntry));
            list->count--;
            found = true;
            break;
        }
    }
    
    if (found) {
        state_lock(state);
        state->dirty_flags |= DIRTY_EXCLUSIONS;
        state->mutation_count++;
        state_unlock(state);
    }
    
    return found;
}

bool service_state_update_config(ServiceState *state, const NcdConfig *config) {
    if (!state || !state->metadata || !config) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    memcpy(&state->metadata->cfg, config, sizeof(NcdConfig));
    state_lock(state);
    state->dirty_flags |= DIRTY_CONFIG;
    state->mutation_count++;
    state_unlock(state);
    
    return true;
}

bool service_state_clear_history(ServiceState *state) {
    if (!state || !state->metadata) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    db_heur_clear(state->metadata);
    db_dir_history_clear(state->metadata);
    
    state_lock(state);
    state->dirty_flags |= DIRTY_HEURISTICS | DIRTY_DIR_HISTORY;
    state->mutation_count++;
    state_unlock(state);
    
    return true;
}

bool service_state_add_dir_history(ServiceState *state, 
                                    const char *path, 
                                    char drive) {
    if (!state || !state->metadata || !path) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    bool result = db_dir_history_add(state->metadata, path, drive);
    if (result) {
        state_lock(state);
        state->dirty_flags |= DIRTY_DIR_HISTORY;
        state->mutation_count++;
        state_unlock(state);
    }
    
    return result;
}

bool service_state_remove_dir_history(ServiceState *state, int index) {
    if (!state || !state->metadata) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    bool result = db_dir_history_remove(state->metadata, index);
    if (result) {
        state_lock(state);
        state->dirty_flags |= DIRTY_DIR_HISTORY;
        state->mutation_count++;
        state_unlock(state);
    }
    
    return result;
}

bool service_state_swap_dir_history(ServiceState *state) {
    if (!state || !state->metadata) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    /* Need at least 2 entries to swap */
    if (state->metadata->dir_history.count < 2) {
        return false;
    }
    
    db_dir_history_swap_first_two(state->metadata);
    state_lock(state);
    state->dirty_flags |= DIRTY_DIR_HISTORY;
    state->mutation_count++;
    state_unlock(state);
    
    return true;
}

bool service_state_update_database(ServiceState *state, NcdDatabase *db, bool is_partial) {
    if (!state || !db) {
        return false;
    }
    
    /* Reject mutations during shutdown */
    if (state->shutdown_requested) {
        return false;
    }
    
    state_lock(state);
    
    /* Release our reference to the old database.
     * It won't be freed if other code (e.g., clients) still holds references. */
    if (state->database) {
        db_free(state->database);
    }
    
    /* Take ownership of new database (already has ref_count = 1 from creation) */
    state->database = db;
    state->last_rescan = time(NULL);
    state->database->last_scan = state->last_rescan;
    
    /* Use different dirty flag for partial vs full rescan */
    if (is_partial) {
        state->dirty_flags |= DIRTY_DATABASE_PARTIAL;
    } else {
        state->dirty_flags |= DIRTY_DATABASE;
    }
    
    state->mutation_count++;
    state->last_db_mutation = time(NULL);
    
    state_unlock(state);
    
    return true;
}

/* --------------------------------------------------------- persistence        */

bool service_state_flush(ServiceState *state) {
    if (!state) {
        return false;
    }
    
    /* Lock state during flush to prevent race conditions with mutations */
    state_lock(state);
    
    if (state->dirty_flags == 0) {
        state_unlock(state);
        return true;  /* Nothing to flush */
    }
    
    /* Capture dirty flags while locked */
    uint32_t flags_to_flush = state->dirty_flags;
    
    state_unlock(state);
    
    bool success = true;
    
    /* Save metadata if dirty (outside lock to prevent blocking mutations during I/O) */
    if (flags_to_flush & DIRTY_METADATA_ALL) {
        if (!db_metadata_save(state->metadata)) {
            success = false;
        }
    }
    
    /* Save database if dirty (either full or partial rescan) */
    if (flags_to_flush & (DIRTY_DATABASE | DIRTY_DATABASE_PARTIAL)) {
        /* Save per-drive databases */
        for (int i = 0; i < state->database->drive_count; i++) {
            DriveData *drv = &state->database->drives[i];
            char path[MAX_PATH];
            
            if (!ncd_platform_db_drive_path(drv->letter, path, sizeof(path))) {
                continue;
            }
            
            /* Create temporary database with just this drive */
            NcdDatabase tmp_db;
            memset(&tmp_db, 0, sizeof(tmp_db));
            tmp_db.version = state->database->version;
            tmp_db.default_show_hidden = state->database->default_show_hidden;
            tmp_db.default_show_system = state->database->default_show_system;
            tmp_db.last_scan = state->database->last_scan;
            tmp_db.drives = drv;
            tmp_db.drive_count = 1;
            tmp_db.drive_capacity = 1;
            
            if (!db_save_binary(&tmp_db, path)) {
                success = false;
            }
        }
    }
    
    /* Update state to clear dirty flags */
    state_lock(state);
    if (success) {
        state->dirty_flags &= ~flags_to_flush;  /* Only clear flags we just flushed */
        state->last_flush = time(NULL);
    }
    state_unlock(state);
    
    return success;
}

bool service_state_needs_flush(const ServiceState *state) {
    if (!state) {
        return false;
    }
    return state->dirty_flags != 0;
}

bool service_state_needs_immediate_flush(const ServiceState *state) {
    if (!state) {
        return false;
    }
    /* Full rescans (DIRTY_DATABASE) need immediate flush.
     * Partial rescans (DIRTY_DATABASE_PARTIAL) can use delayed flush.
     * Metadata changes can also use delayed flush.
     */
    return (state->dirty_flags & DIRTY_DATABASE) != 0;
}

void service_state_note_db_mutation(ServiceState *state) {
    if (!state) {
        return;
    }
    state->last_db_mutation = time(NULL);
    state->mutation_count++;
}

time_t service_state_get_db_mutation_age(const ServiceState *state) {
    if (!state || state->last_db_mutation == 0) {
        return (time_t)INT_MAX;  /* No mutation has ever occurred */
    }
    return time(NULL) - state->last_db_mutation;
}

/* --------------------------------------------------------- snapshot generation */

uint64_t service_state_get_meta_generation(const ServiceState *state) {
    if (!state) {
        return 0;
    }
    return state->meta_generation;
}

uint64_t service_state_get_db_generation(const ServiceState *state) {
    if (!state) {
        return 0;
    }
    return state->db_generation;
}

uint64_t service_state_bump_meta_generation(ServiceState *state) {
    if (!state) {
        return 0;
    }
    return ++state->meta_generation;
}

uint64_t service_state_bump_db_generation(ServiceState *state) {
    if (!state) {
        return 0;
    }
    return ++state->db_generation;
}

/* --------------------------------------------------------- statistics         */

void service_state_get_stats(const ServiceState *state, ServiceStats *stats) {
    if (!state || !stats) {
        return;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    stats->meta_generation = state->meta_generation;
    stats->db_generation = state->db_generation;
    stats->dirty_flags = state->dirty_flags;
    stats->last_flush = state->last_flush;
    stats->last_rescan = state->last_rescan;
    stats->request_count = state->request_count;
    stats->mutation_count = state->mutation_count;
    
    if (state->database) {
        stats->drive_count = state->database->drive_count;
        for (int i = 0; i < state->database->drive_count; i++) {
            stats->total_dirs += state->database->drives[i].dir_count;
        }
    }
}

/* --------------------------------------------------------- utility            */

uint32_t service_state_get_dirty_flags(const ServiceState *state) {
    if (!state) {
        return 0;
    }
    return state->dirty_flags;
}

void service_state_clear_dirty(ServiceState *state, uint32_t flags) {
    if (!state) {
        return;
    }
    state->dirty_flags &= ~flags;
}

/* --------------------------------------------------------- runtime state        */

void service_state_set_runtime_state(ServiceState *state, ServiceRuntimeState new_state) {
    if (!state) return;
    state_lock(state);
    state->runtime_state = new_state;
    state_unlock(state);
}

ServiceRuntimeState service_state_get_runtime_state(const ServiceState *state) {
    if (!state) return SERVICE_STATE_STOPPED;
    state_lock((ServiceState *)state);  /* Cast away const for lock */
    ServiceRuntimeState result = state->runtime_state;
    state_unlock((ServiceState *)state);
    return result;
}

bool service_state_wait_for_ready(ServiceState *state, int timeout_ms) {
    if (!state) return false;
    
    /* Simple polling implementation */
    int waited = 0;
    while (state->runtime_state != SERVICE_STATE_READY && waited < timeout_ms) {
        platform_sleep_ms(10);
        waited += 10;
    }
    
    return state->runtime_state == SERVICE_STATE_READY;
}

bool service_state_has_database_data(const ServiceState *state) {
    if (!state || !state->database) {
        return false;
    }
    
    /* Check if we have at least one drive with directories */
    if (state->database->drive_count == 0) {
        return false;
    }
    
    /* Check if any drive has directories */
    for (int i = 0; i < state->database->drive_count; i++) {
        if (state->database->drives[i].dir_count > 0) {
            return true;
        }
    }
    
    return false;
}

void service_state_set_status_message(ServiceState *state, const char *message) {
    if (!state || !message) return;
    state_lock(state);
    strncpy(state->status_message, message, sizeof(state->status_message) - 1);
    state->status_message[sizeof(state->status_message) - 1] = '\0';
    state_unlock(state);
}

const char *service_state_get_status_message(const ServiceState *state) {
    if (!state) return "Unknown";
    return state->status_message;
}

bool service_state_request_shutdown(ServiceState *state) {
    if (!state) return false;
    state_lock(state);
    bool was_not_requested = !state->shutdown_requested;
    state->shutdown_requested = 1;
    state_unlock(state);
    return was_not_requested;
}

bool service_state_is_shutdown_requested(const ServiceState *state) {
    if (!state) return false;
    return state->shutdown_requested != 0;
}

bool service_state_is_available_for_operations(const ServiceState *state) {
    if (!state) return false;
    state_lock((ServiceState *)state);  /* Cast away const for lock */
    bool available = (state->runtime_state == SERVICE_STATE_READY && 
                      !state->shutdown_requested);
    state_unlock((ServiceState *)state);
    return available;
}

/* --------------------------------------------------------- request queue        */

bool service_state_enqueue_request(ServiceState *state, 
                                    PendingRequestType type,
                                    const void *data,
                                    size_t data_len) {
    if (!state) return false;
    
    /* Check queue limit */
    if (state->pending_count >= state->pending_max) {
        return false;
    }
    
    /* Allocate node */
    PendingRequestNode *node = (PendingRequestNode *)calloc(1, sizeof(PendingRequestNode));
    if (!node) return false;
    
    node->type = type;
    node->next = NULL;
    
    /* Copy data if provided */
    if (data && data_len > 0) {
        node->data = malloc(data_len);
        if (!node->data) {
            free(node);
            return false;
        }
        memcpy(node->data, data, data_len);
        node->data_len = data_len;
    }
    
    /* Add to tail */
    if (state->pending_tail) {
        state->pending_tail->next = node;
    } else {
        state->pending_head = node;
    }
    state->pending_tail = node;
    state->pending_count++;
    
    return true;
}

/*
 * service_state_dequeue_pending  --  Dequeue a pending request for processing
 *
 * Returns true if a request was dequeued, false if queue is empty.
 * Caller is responsible for freeing node->data and node.
 */
bool service_state_dequeue_pending(ServiceState *state, PendingRequestType *out_type,
                                    void **out_data, size_t *out_data_len) {
    if (!state || !out_type || !out_data || !out_data_len) return false;
    
    if (!state->pending_head) return false;
    
    PendingRequestNode *node = state->pending_head;
    state->pending_head = node->next;
    if (!state->pending_head) {
        state->pending_tail = NULL;
    }
    state->pending_count--;
    
    *out_type = node->type;
    *out_data = node->data;  /* Transfer ownership */
    *out_data_len = node->data_len;
    
    free(node);
    return true;
}

void service_state_process_pending(ServiceState *state, void *pub) {
    if (!state) return;
    (void)pub;
    
    /* Process and dequeue all pending requests */
    PendingRequestType type;
    void *data;
    size_t data_len;
    
    while (service_state_dequeue_pending(state, &type, &data, &data_len)) {
        /* In the stub implementation, we just free the data */
        /* The actual processing is implemented in service_main.c */
        if (data) {
            free(data);
        }
    }
}

void service_state_clear_pending(ServiceState *state) {
    if (!state) return;
    
    while (state->pending_head) {
        PendingRequestNode *node = state->pending_head;
        state->pending_head = node->next;
        
        if (node->data) {
            free(node->data);
        }
        free(node);
    }
    
    state->pending_tail = NULL;
    state->pending_count = 0;
}

int service_state_get_pending_count(const ServiceState *state) {
    if (!state) return 0;
    return state->pending_count;
}
