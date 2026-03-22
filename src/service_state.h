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

/* Dirty flags for tracking changes */
typedef enum {
    DIRTY_NONE          = 0x00,
    DIRTY_CONFIG        = 0x01,
    DIRTY_GROUPS        = 0x02,
    DIRTY_HEURISTICS    = 0x04,
    DIRTY_EXCLUSIONS    = 0x08,
    DIRTY_DIR_HISTORY   = 0x10,
    DIRTY_METADATA_ALL  = 0x1F,  /* All metadata flags */
    DIRTY_DATABASE      = 0x20,
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
 * Loads metadata and database from disk, creates initial snapshots.
 * Returns state handle on success, NULL on failure.
 */
ServiceState *service_state_init(void);

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
 * service_state_update_database  --  Replace database after rescan
 *
 * Takes ownership of the database (caller must not use after call).
 */
bool service_state_update_database(ServiceState *state, NcdDatabase *db);

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
