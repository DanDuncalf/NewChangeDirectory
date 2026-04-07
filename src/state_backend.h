/*
 * state_backend.h  --  State access abstraction for NCD
 *
 * This module provides a unified interface for obtaining metadata and database
 * state, whether from local disk (standalone mode) or from a shared memory
 * service (service-backed mode).
 *
 * The abstraction allows main.c and other client code to remain unchanged
 * regardless of where state comes from.
 */

#ifndef NCD_STATE_BACKEND_H
#define NCD_STATE_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"

/* --------------------------------------------------------- types            */

/*
 * NcdStateSourceInfo  --  Information about where state came from
 */
typedef struct {
    bool from_service;        /* true if from service, false if local disk */
    uint64_t generation;      /* snapshot generation (0 if not applicable) */
    uint64_t db_generation;   /* database generation (0 if not applicable) */
} NcdStateSourceInfo;

/*
 * NcdStateView  --  A unified state view structure for both local and service modes
 *
 * This structure can represent state from either local disk (standalone mode)
 * or from a shared memory service. The from_service flag determines which
 * fields are valid.
 *
 * NOTE: This is the ZERO-COPY refactor version. Service mode uses direct pointers
 * into mapped shared memory instead of embedded copies.
 */
typedef struct NcdStateView {
    /* Common header - always valid */
    NcdStateSourceInfo info;
    
    /* Mode-specific data - accessed via accessor functions only */
    union {
        /* Local mode data (info.from_service == false) */
        struct {
            NcdMetadata *metadata;     /* Owned by this view (heap allocated) */
            NcdDatabase *database;     /* Owned by this view (heap allocated) */
            bool metadata_loaded;
            bool database_loaded;
            bool metadata_dirty;       /* Track if metadata needs save */
        } local;
        
        /* Service mode data (info.from_service == true) - ZERO COPY */
        struct {
            void *ipc_client;          /* NcdIpcClient* (opaque) */
            void *meta_shm;            /* ShmHandle* */
            void *db_shm;              /* ShmHandle* */
            void *meta_addr;           /* Mapped shared memory base */
            void *db_addr;             /* Mapped shared memory base */
            size_t meta_size;
            size_t db_size;
            
            /* 
             * ZERO-COPY: Pointers directly into shared memory.
             * 
             * metadata_ptr points to ShmMetadataHeader + sections.
             * Use SHM_PTR macros to access actual sections.
             * 
             * database_ptr points to ShmDatabaseHeader + mount entries.
             * Use SHM_MOUNT_* macros to access mount data.
             */
            void *metadata_ptr;        /* Direct pointer to metadata snapshot */
            void *database_ptr;        /* Direct pointer to database snapshot */
            
            /* 
             * Minimal view structures pointing to shared memory.
             * These are initialized to reference SHM data via pointers.
             */
            NcdMetadata *metadata_view;  /* Points to parsed metadata (may reference SHM) */
            NcdDatabase *database_view;  /* Points to parsed database (references SHM) */
            
            bool has_metadata;
            bool has_database;
        } service;
    } data;
} NcdStateView;

/* --------------------------------------------------------- lifecycle        */

/*
 * state_backend_open_best_effort  --  Open state from best available source
 *
 * Attempts to connect to the service first. If service is unavailable or
 * returns an error, falls back to loading from local disk.
 *
 * On success, fills 'out' with a state view handle and 'info' with source
 * information. Returns 0 on success, non-zero on error.
 *
 * The caller must call state_backend_close() when done.
 */
int state_backend_open_best_effort(NcdStateView **out, NcdStateSourceInfo *info);

/*
 * state_backend_open_local  --  Open state from local disk only
 *
 * Loads metadata and database directly from disk files, bypassing any
 * service. Use this when you explicitly want standalone mode.
 *
 * On success, fills 'out' with a state view handle and 'info' with source
 * information. Returns 0 on success, non-zero on error.
 *
 * The caller must call state_backend_close() when done.
 */
int state_backend_open_local(NcdStateView **out, NcdStateSourceInfo *info);

/*
 * state_backend_close  --  Close a state view and release resources
 *
 * This must be called for every successful open call. It handles unmapping
 * shared memory (if service-backed) or freeing heap memory (if local).
 */
void state_backend_close(NcdStateView *view);

/*
 * state_backend_error_string  --  Get the last error message
 *
 * Returns a human-readable error string describing the last failure.
 * The string is valid until the next call to a state_backend function.
 */
const char *state_backend_error_string(void);

/* --------------------------------------------------------- state access     */

/*
 * state_view_metadata  --  Get metadata from a state view
 *
 * Returns a pointer to the metadata. The pointer is valid until
 * state_backend_close() is called. Returns NULL if metadata is not available.
 *
 * Note: The returned metadata is read-only for service-backed views.
 * Modifications must go through state_backend_submit_metadata_update().
 */
const NcdMetadata *state_view_metadata(const NcdStateView *view);

/*
 * state_view_database  --  Get database from a state view
 *
 * Returns a pointer to the database. The pointer is valid until
 * state_backend_close() is called. Returns NULL if database is not available.
 *
 * Note: The returned database is read-only. The database may be lazily
 * loaded from shared memory or may point into a mapped region.
 */
const NcdDatabase *state_view_database(const NcdStateView *view);

/* --------------------------------------------------------- mutations        */

/*
 * state_backend_submit_heuristic_update  --  Submit a heuristic update
 *
 * Notifies the service (if active) that the user selected a target for a
 * search term. This updates the frequency/last_used tracking.
 *
 * If service-backed: sends update via IPC.
 * If standalone: applies directly to metadata and saves to disk.
 *
 * Returns 0 on success, non-zero on error.
 */
int state_backend_submit_heuristic_update(NcdStateView *view,
                                          const char *search,
                                          const char *target);

/*
 * state_backend_submit_metadata_update  --  Submit a metadata change
 *
 * Notifies the service (if active) of a metadata change (group add/remove,
 * exclusion add/remove, config change, etc.).
 *
 * If service-backed: sends update via IPC.
 * If standalone: applies directly to metadata and saves to disk.
 *
 * Returns 0 on success, non-zero on error.
 */
int state_backend_submit_metadata_update(NcdStateView *view,
                                         int update_type,
                                         const void *data,
                                         size_t data_size);

/* Metadata update types */
#define NCD_META_UPDATE_GROUP_ADD       1
#define NCD_META_UPDATE_GROUP_REMOVE    2
#define NCD_META_UPDATE_EXCLUSION_ADD   3
#define NCD_META_UPDATE_EXCLUSION_REMOVE 4
#define NCD_META_UPDATE_CONFIG          5
#define NCD_META_UPDATE_CLEAR_HISTORY   6
#define NCD_META_UPDATE_DIR_HISTORY_ADD 7
#define NCD_META_UPDATE_DIR_HISTORY_REMOVE 8
#define NCD_META_UPDATE_DIR_HISTORY_SWAP   9

/*
 * state_backend_request_rescan  --  Request a database rescan
 *
 * Asks the service (if active) to rescan drives. If standalone, performs
 * the rescan directly.
 *
 * Returns 0 on success, non-zero on error.
 */
int state_backend_request_rescan(NcdStateView *view,
                                 const bool drive_mask[26],
                                 bool scan_root_only);

/*
 * state_backend_request_flush  --  Request immediate persistence
 *
 * Asks the service (if active) to flush any pending changes to disk.
 * If standalone, this is a no-op (changes are already persisted).
 *
 * Returns 0 on success, non-zero on error.
 */
int state_backend_request_flush(NcdStateView *view);

/* --------------------------------------------------------- utilities        */

/*
 * state_backend_get_source_info  --  Get source info from a view
 *
 * Fills 'info' with information about where this view's state came from.
 */
void state_backend_get_source_info(const NcdStateView *view,
                                   NcdStateSourceInfo *info);

/*
 * state_backend_error_string  --  Get human-readable error message
 *
 * Returns a static string describing the last error that occurred.
 * This is not thread-safe.
 */
const char *state_backend_error_string(void);

#ifdef __cplusplus
}
#endif

#endif /* NCD_STATE_BACKEND_H */
