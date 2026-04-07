/*
 * state_backend_local.c  --  Local disk state backend implementation
 *
 * This implements the state_backend interface by loading metadata and
 * database directly from disk files. This is the standalone fallback path
 * when no service is available.
 */

#include "state_backend.h"
#include "database.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* --------------------------------------------------------- local state      */

static char g_last_error[256] = {0};

/* Convenience macros for accessing local mode fields */
#define LOCAL(view) ((view)->data.local)

/* Service backend entry points (implemented in state_backend_service.c) */
extern void state_backend_close_service(NcdStateView *view);
extern const NcdMetadata *state_view_metadata_service(const NcdStateView *view);
extern const NcdDatabase *state_view_database_service(const NcdStateView *view);
extern int state_backend_submit_heuristic_update_service(NcdStateView *view,
                                                         const char *search,
                                                         const char *target);
extern int state_backend_submit_metadata_update_service(NcdStateView *view,
                                                        int update_type,
                                                        const void *data,
                                                        size_t data_size);
extern int state_backend_request_rescan_service(NcdStateView *view,
                                                const bool drive_mask[26],
                                                bool scan_root_only);
extern int state_backend_request_flush_service(NcdStateView *view);
extern const char *state_backend_service_error_string(void);

/* --------------------------------------------------------- error handling   */

static void set_error(const char *msg) {
    strncpy(g_last_error, msg, sizeof(g_last_error) - 1);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
}

const char *state_backend_error_string(void) {
    return g_last_error;
}

/* --------------------------------------------------------- internal helpers */

/*
 * Load all per-drive databases and merge into a single NcdDatabase
 */
static NcdDatabase *load_all_drive_databases(void) {
    NcdDatabase *db = db_create();
    if (!db) {
        set_error("Failed to create database");
        return NULL;
    }

    /* Get available drives */
    char drives[26];
    int drive_count = platform_get_available_drives(drives, 26);
    
    for (int i = 0; i < drive_count; i++) {
        char path[MAX_PATH];
        if (!ncd_platform_db_drive_path(drives[i], path, sizeof(path))) {
            continue;
        }

        /* Check if file exists by trying to open it */
        FILE *f = fopen(path, "rb");
        if (!f) {
            continue;  /* No database for this drive */
        }
        fclose(f);

        /* Load this drive's database */
        NcdDatabase *drive_db = db_load_auto(path);
        if (!drive_db) {
            continue;  /* Failed to load, skip this drive */
        }

        /* Merge drive data into main database */
        for (int d = 0; d < drive_db->drive_count; d++) {
            DriveData *src = &drive_db->drives[d];
            DriveData *dst = db_add_drive(db, src->letter);
            if (!dst) {
                continue;
            }

            /* Copy drive properties */
            dst->type = src->type;
            memcpy(dst->label, src->label, sizeof(dst->label));

            /* Copy all directories */
            for (int dir_idx = 0; dir_idx < src->dir_count; dir_idx++) {
                DirEntry *entry = &src->dirs[dir_idx];
                const char *name = src->name_pool + entry->name_off;
                db_add_dir(dst, name, entry->parent, 
                          entry->is_hidden, entry->is_system);
            }
        }

        /* Free the temporary drive database */
        /* Note: drive_db may have blob_buf, so use db_free */
        db_free(drive_db);
    }

    return db;
}

/*
 * Save metadata to disk if dirty
 */
static bool save_metadata_if_dirty(NcdStateView *view) {
    if (!LOCAL(view).metadata_dirty || !LOCAL(view).metadata) {
        return true;
    }

    if (!db_metadata_save(LOCAL(view).metadata)) {
        set_error("Failed to save metadata");
        return false;
    }

    LOCAL(view).metadata_dirty = false;
    return true;
}

/* --------------------------------------------------------- lifecycle        */

int state_backend_open_local(NcdStateView **out, NcdStateSourceInfo *info) {
    if (!out) {
        set_error("Invalid output pointer");
        return -1;
    }

    *out = NULL;
    memset(g_last_error, 0, sizeof(g_last_error));

    /* Allocate view structure */
    NcdStateView *view = (NcdStateView *)ncd_calloc(1, sizeof(NcdStateView));
    if (!view) {
        set_error("Out of memory");
        return -1;
    }

    /* Mark as local */
    view->info.from_service = false;
    view->info.generation = 0;
    view->info.db_generation = 0;

    /* Load metadata */
    LOCAL(view).metadata = db_metadata_load();
    if (!LOCAL(view).metadata) {
        /* Create empty metadata on failure */
        LOCAL(view).metadata = db_metadata_create();
        if (!LOCAL(view).metadata) {
            set_error("Failed to create metadata");
            free(view);
            return -1;
        }
    }
    LOCAL(view).metadata_loaded = true;

    /* Load database (all drives) */
    LOCAL(view).database = load_all_drive_databases();
    if (!LOCAL(view).database) {
        /* Non-fatal: database might not exist yet */
        LOCAL(view).database = db_create();
    }
    LOCAL(view).database_loaded = (LOCAL(view).database != NULL);

    /* Fill output info */
    if (info) {
        *info = view->info;
    }

    *out = view;
    return 0;
}

void state_backend_close(NcdStateView *view) {
    if (!view) {
        return;
    }

    if (view->info.from_service) {
        state_backend_close_service(view);
        return;
    }

    /* Save metadata if dirty */
    if (LOCAL(view).metadata_dirty && LOCAL(view).metadata) {
        save_metadata_if_dirty(view);
    }

    /* Free metadata */
    if (LOCAL(view).metadata) {
        db_metadata_free(LOCAL(view).metadata);
    }

    /* Free database */
    if (LOCAL(view).database) {
        db_free(LOCAL(view).database);
    }

    /* Free view structure */
    free(view);
}

/* --------------------------------------------------------- state access     */

const NcdMetadata *state_view_metadata(const NcdStateView *view) {
    if (!view) {
        return NULL;
    }
    if (view->info.from_service) {
        return state_view_metadata_service(view);
    }
    if (!LOCAL(view).metadata_loaded) {
        return NULL;
    }
    return LOCAL(view).metadata;
}

const NcdDatabase *state_view_database(const NcdStateView *view) {
    if (!view) {
        return NULL;
    }
    if (view->info.from_service) {
        return state_view_database_service(view);
    }
    if (!LOCAL(view).database_loaded) {
        return NULL;
    }
    return LOCAL(view).database;
}

/* --------------------------------------------------------- mutations        */

int state_backend_submit_heuristic_update(NcdStateView *view,
                                          const char *search,
                                          const char *target) {
    if (!view) {
        set_error("No metadata available");
        return -1;
    }

    if (view->info.from_service) {
        return state_backend_submit_heuristic_update_service(view, search, target);
    }

    if (!LOCAL(view).metadata) {
        set_error("No metadata available");
        return -1;
    }

    /* Local mode: update directly and mark dirty */
    db_heur_note_choice((NcdMetadata *)LOCAL(view).metadata, search, target);
    LOCAL(view).metadata_dirty = true;

    /* Auto-save in local mode */
    if (!save_metadata_if_dirty(view)) {
        return -1;
    }

    return 0;
}

int state_backend_submit_metadata_update(NcdStateView *view,
                                         int update_type,
                                         const void *data,
                                         size_t data_size) {
    if (!view) {
        set_error("No metadata available");
        return -1;
    }

    if (view->info.from_service) {
        return state_backend_submit_metadata_update_service(view, update_type, data, data_size);
    }

    if (!LOCAL(view).metadata) {
        set_error("No metadata available");
        return -1;
    }

    NcdMetadata *meta = (NcdMetadata *)LOCAL(view).metadata;
    bool changed = false;

    switch (update_type) {
        case NCD_META_UPDATE_GROUP_ADD: {
            if (data_size >= sizeof(const char *) * 2) {
                const char **args = (const char **)data;
                changed = db_group_set(meta, args[0], args[1]);
            }
            break;
        }
        case NCD_META_UPDATE_GROUP_REMOVE: {
            if (data && data_size > 0) {
                changed = db_group_remove(meta, (const char *)data);
            }
            break;
        }
        case NCD_META_UPDATE_EXCLUSION_ADD: {
            if (data && data_size > 0) {
                changed = db_exclusion_add(meta, (const char *)data);
            }
            break;
        }
        case NCD_META_UPDATE_EXCLUSION_REMOVE: {
            if (data && data_size > 0) {
                /* Find and remove exclusion by pattern */
                for (int i = 0; i < meta->exclusions.count; i++) {
                    if (strcmp(meta->exclusions.entries[i].pattern, 
                               (const char *)data) == 0) {
                        /* Remove by shifting */
                        memmove(&meta->exclusions.entries[i],
                                &meta->exclusions.entries[i + 1],
                                (meta->exclusions.count - i - 1) * 
                                sizeof(NcdExclusionEntry));
                        meta->exclusions.count--;
                        changed = true;
                        break;
                    }
                }
            }
            break;
        }
        case NCD_META_UPDATE_CONFIG: {
            if (data_size >= sizeof(NcdConfig)) {
                memcpy(&meta->cfg, data, sizeof(NcdConfig));
                meta->config_dirty = true;
                changed = true;
            }
            break;
        }
        case NCD_META_UPDATE_CLEAR_HISTORY: {
            db_heur_clear(meta);
            db_dir_history_clear(meta);
            changed = true;
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_ADD: {
            if (data_size >= sizeof(const char *) + sizeof(char)) {
                const char *path = *(const char **)data;
                char drive = *((const char *)data + sizeof(const char *));
                changed = db_dir_history_add(meta, path, drive);
            }
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_REMOVE: {
            if (data_size >= sizeof(int)) {
                int idx = *(const int *)data;
                changed = db_dir_history_remove(meta, idx);
            }
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_SWAP: {
            /* Swap first two entries */
            if (meta->dir_history.count >= 2) {
                db_dir_history_swap_first_two(meta);
                changed = true;
            }
            break;
        }
        default:
            set_error("Unknown metadata update type");
            return -1;
    }

    if (changed) {
        LOCAL(view).metadata_dirty = true;
        if (!save_metadata_if_dirty(view)) {
            return -1;
        }
    }

    return 0;
}

int state_backend_request_rescan(NcdStateView *view,
                                 const bool drive_mask[26],
                                 bool scan_root_only) {
    if (view && view->info.from_service) {
        return state_backend_request_rescan_service(view, drive_mask, scan_root_only);
    }

    /* In local mode, we don't handle rescan here */
    /* The caller (main.c) should perform the rescan directly */
    (void)view;
    (void)drive_mask;
    (void)scan_root_only;
    return 0;
}

int state_backend_request_flush(NcdStateView *view) {
    if (view && view->info.from_service) {
        return state_backend_request_flush_service(view);
    }

    /* In local mode, flush just saves metadata if dirty */
    if (view && !view->info.from_service && LOCAL(view).metadata_dirty) {
        if (!save_metadata_if_dirty(view)) {
            return -1;
        }
    }
    return 0;
}

/* --------------------------------------------------------- utilities        */

void state_backend_get_source_info(const NcdStateView *view,
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

/* --------------------------------------------------------- best effort      */

/* Forward declaration for service backend (implemented in state_backend_service.c) */
extern int state_backend_try_service(NcdStateView **out, NcdStateSourceInfo *info);
extern bool state_backend_service_available(void);

int state_backend_open_best_effort(NcdStateView **out, NcdStateSourceInfo *info) {
    if (!out) {
        set_error("Invalid output pointer");
        return -1;
    }

    *out = NULL;
    
    /* Try service first if available */
    if (state_backend_service_available()) {
        int result = state_backend_try_service(out, info);
        if (result == 0) {
            return 0;  /* Service connection successful */
        }
        /* Service exists but is busy (loading/scanning) - do NOT fall back */
        const char *service_err = state_backend_service_error_string();
        if (service_err && strstr(service_err, "not yet available")) {
            /* Copy service error to local error buffer */
            set_error(service_err);
            /* Propagate the error up - client should know service is busy */
            return -1;
        }
        /* For other errors (version mismatch, etc.), fall back to local */
    }

    /* Fall back to local mode */
    return state_backend_open_local(out, info);
}
