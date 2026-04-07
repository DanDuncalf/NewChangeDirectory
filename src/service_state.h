/*
 * service_state.h  --  Service state management for NCD
 *
 * This module manages the live in-memory state of the NCD service:
 * - Metadata (config, groups, heuristics, exclusions, dir history)
 * - Database (directory entries for all drives)
 * - Dirty tracking for lazy persistence
 * - Snapshot generation management
 */

#ifndef NCD_SERVICE_STATE_H
#define NCD_SERVICE_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"
#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------- types              */

typedef struct ServiceState ServiceState;

/* Service runtime states for lazy loading */
typedef enum {
    SERVICE_STATE_STOPPED = 0,
    SERVICE_STATE_STARTING,
    SERVICE_STATE_LOADING,
    SERVICE_STATE_READY,
    SERVICE_STATE_SCANNING
} ServiceRuntimeState;

/* Dirty flags for tracking changes */
typedef enum {
    DIRTY_NONE             = 0x00,
    DIRTY_CONFIG           = 0x01,
    DIRTY_GROUPS           = 0x02,
    DIRTY_HEURISTICS       = 0x04,
    DIRTY_EXCLUSIONS       = 0x08,
    DIRTY_DIR_HISTORY      = 0x10,
    DIRTY_METADATA_ALL     = 0x1F,  /* All metadata flags */
    DIRTY_DATABASE         = 0x20,  /* Full rescan - flush immediately */
    DIRTY_DATABASE_PARTIAL = 0x40,  /* Partial/subdir scan - use delayed flush */
} DirtyFlags;

/* Service statistics */
typedef struct {
    uint64_t meta_generation;
    uint64_t db_generation;
    uint32_t dirty_flags;
    size_t   meta_size;
    size_t   db_size;
    int      drive_count;
    int      total_dirs;
    time_t   last_flush;
    time_t   last_rescan;
    uint64_t request_count;
    uint64_t mutation_count;
} ServiceStats;

/* --------------------------------------------------------- lifecycle          */

/*
 * service_state_init  --  Initialize service state
 *
 * Loads metadata from disk synchronously. Database is loaded lazily
 * via service_state_load_databases().
 * Returns state handle on success, NULL on failure.
 */
ServiceState *service_state_init(void);

/*
 * service_state_load_databases  --  Load all drive databases
 *
 * This is called by the background loader thread. Loads per-drive
 * databases from disk and merges them into the main database.
 * Returns true on success, false on error.
 */
bool service_state_load_databases(ServiceState *state);

/*
 * service_state_cleanup  --  Cleanup service state
 *
 * Flushes any pending changes to disk and releases all resources.
 */
void service_state_cleanup(ServiceState *state);

/* --------------------------------------------------------- state access       */

/*
 * service_state_get_metadata  --  Get current metadata
 *
 * Returns pointer to in-memory metadata. This is the authoritative
 * copy that the service maintains. Caller must not modify.
 */
const NcdMetadata *service_state_get_metadata(const ServiceState *state);

/*
 * service_state_get_database  --  Get current database
 *
 * Returns pointer to in-memory database. This is the authoritative
 * copy that the service maintains. Caller must not modify.
 */
const NcdDatabase *service_state_get_database(const ServiceState *state);

/* --------------------------------------------------------- mutations          */

/*
 * service_state_note_heuristic  --  Record a user choice
 *
 * Updates heuristics for search->target pair and marks dirty.
 */
bool service_state_note_heuristic(ServiceState *state,
                                   const char *search,
                                   const char *target);

/*
 * service_state_add_group  --  Add or update a group
 */
bool service_state_add_group(ServiceState *state,
                              const char *name,
                              const char *path);

/*
 * service_state_remove_group  --  Remove a group
 */
bool service_state_remove_group(ServiceState *state, const char *name);

/*
 * service_state_add_exclusion  --  Add an exclusion pattern
 */
bool service_state_add_exclusion(ServiceState *state, const char *pattern);

/*
 * service_state_remove_exclusion  --  Remove an exclusion pattern
 */
bool service_state_remove_exclusion(ServiceState *state, const char *pattern);

/*
 * service_state_update_config  --  Update configuration
 */
bool service_state_update_config(ServiceState *state, const NcdConfig *config);

/*
 * service_state_clear_history  --  Clear all history
 */
bool service_state_clear_history(ServiceState *state);

/*
 * service_state_add_dir_history  --  Add entry to directory history
 */
bool service_state_add_dir_history(ServiceState *state, 
                                    const char *path, 
                                    char drive);

/*
 * service_state_remove_dir_history  --  Remove entry from directory history by index
 */
bool service_state_remove_dir_history(ServiceState *state, int index);

/*
 * service_state_swap_dir_history  --  Swap first two entries (ping-pong)
 */
bool service_state_swap_dir_history(ServiceState *state);

/*
 * service_state_update_database  --  Replace database after rescan
 *
 * Takes ownership of the database (caller must not use after call).
 * 
 * is_partial: true for subdirectory scan (/r.), false for full rescan
 *             Partial scans use delayed flush, full scans flush immediately
 */
bool service_state_update_database(ServiceState *state, NcdDatabase *db, bool is_partial);

/* --------------------------------------------------------- persistence        */

/*
 * service_state_flush  --  Persist dirty state to disk
 *
 * Saves any dirty metadata or database to disk using atomic
 * temp-file-then-rename semantics.
 */
bool service_state_flush(ServiceState *state);

/*
 * service_state_needs_flush  --  Check if flush is needed
 */
bool service_state_needs_flush(const ServiceState *state);

/*
 * service_state_needs_immediate_flush  --  Check if immediate flush is required
 *
 * Returns true if a full rescan (DIRTY_DATABASE) is pending, which should
 * be flushed immediately rather than waiting for the periodic interval.
 * Partial rescans and metadata changes can use delayed flush.
 */
bool service_state_needs_immediate_flush(const ServiceState *state);

/*
 * service_state_note_db_mutation  --  Record that a database mutation occurred
 *
 * Updates the last_db_mutation timestamp to the current time.
 * This resets the deferred flush timer.
 */
void service_state_note_db_mutation(ServiceState *state);

/*
 * service_state_get_db_mutation_age  --  Get seconds since last database mutation
 *
 * Returns the number of seconds since the last database mutation.
 * If no mutation has occurred, returns a large value.
 */
time_t service_state_get_db_mutation_age(const ServiceState *state);

/* --------------------------------------------------------- snapshot generation */

/*
 * service_state_get_meta_generation  --  Get current metadata generation
 */
uint64_t service_state_get_meta_generation(const ServiceState *state);

/*
 * service_state_get_db_generation  --  Get current database generation
 */
uint64_t service_state_get_db_generation(const ServiceState *state);

/*
 * service_state_bump_meta_generation  --  Increment metadata generation
 *
 * Called after metadata changes are applied.
 */
uint64_t service_state_bump_meta_generation(ServiceState *state);

/*
 * service_state_bump_db_generation  --  Increment database generation
 *
 * Called after database is updated.
 */
uint64_t service_state_bump_db_generation(ServiceState *state);

/* --------------------------------------------------------- statistics         */

/*
 * service_state_get_stats  --  Get service statistics
 */
void service_state_get_stats(const ServiceState *state, ServiceStats *stats);

/* --------------------------------------------------------- runtime state        */

/*
 * service_state_set_runtime_state  --  Set the service runtime state
 */
void service_state_set_runtime_state(ServiceState *state, ServiceRuntimeState new_state);

/*
 * service_state_get_runtime_state  --  Get the current service runtime state
 */
ServiceRuntimeState service_state_get_runtime_state(const ServiceState *state);

/*
 * service_state_wait_for_ready  --  Wait for service to become READY
 * 
 * Returns true if service became READY, false if timeout.
 */
bool service_state_wait_for_ready(ServiceState *state, int timeout_ms);

/*
 * service_state_has_database_data  --  Check if database has any data
 *
 * Returns true if the database has at least one drive with directories.
 * Used to determine if this is an initial scan (no data) vs a rescan.
 */
bool service_state_has_database_data(const ServiceState *state);

/*
 * service_state_set_status_message  --  Set human-readable status message
 */
void service_state_set_status_message(ServiceState *state, const char *message);

/*
 * service_state_get_status_message  --  Get current status message
 */
const char *service_state_get_status_message(const ServiceState *state);

/* --------------------------------------------------------- request queue        */

/* Request types that can be queued */
typedef enum {
    PENDING_HEURISTIC,
    PENDING_METADATA_UPDATE,
    PENDING_RESCAN,
    PENDING_FLUSH
} PendingRequestType;

/*
 * service_state_enqueue_request  --  Queue a request for later processing
 *
 * Returns true if queued successfully, false if queue is full.
 */
bool service_state_enqueue_request(ServiceState *state, 
                                    PendingRequestType type,
                                    const void *data,
                                    size_t data_len);

/*
 * service_state_process_pending  --  Process all queued requests
 *
 * Should be called after transitioning to READY state.
 */
void service_state_process_pending(ServiceState *state, void *pub);

/*
 * service_state_dequeue_pending  --  Dequeue a pending request for processing
 *
 * Returns true if a request was dequeued, false if queue is empty.
 * Caller is responsible for freeing the returned data.
 */
bool service_state_dequeue_pending(ServiceState *state, PendingRequestType *out_type,
                                    void **out_data, size_t *out_data_len);

/*
 * service_state_clear_pending  --  Clear all pending requests
 */
void service_state_clear_pending(ServiceState *state);

/*
 * service_state_get_pending_count  --  Get number of pending requests
 */
int service_state_get_pending_count(const ServiceState *state);

/* --------------------------------------------------------- utility            */

/*
 * service_state_get_dirty_flags  --  Get current dirty flags
 */
uint32_t service_state_get_dirty_flags(const ServiceState *state);

/*
 * service_state_clear_dirty  --  Clear dirty flags
 */
void service_state_clear_dirty(ServiceState *state, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* NCD_SERVICE_STATE_H */
