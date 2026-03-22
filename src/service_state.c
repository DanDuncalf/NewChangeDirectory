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

/* --------------------------------------------------------- internal state     */

struct ServiceState {
    NcdMetadata *metadata;
    NcdDatabase *database;
    
    uint64_t meta_generation;
    uint64_t db_generation;
    
    uint32_t dirty_flags;
    
    time_t last_flush;
    time_t last_rescan;
    
    uint64_t request_count;
    uint64_t mutation_count;
};

/* --------------------------------------------------------- lifecycle          */

ServiceState *service_state_init(void) {
    ServiceState *state = (ServiceState *)calloc(1, sizeof(ServiceState));
    if (!state) {
        return NULL;
    }
    
    /* Load metadata from disk */
    state->metadata = db_metadata_load();
    if (!state->metadata) {
        state->metadata = db_metadata_create();
        if (!state->metadata) {
            free(state);
            return NULL;
        }
    }
    
    /* Load all drive databases */
    state->database = db_create();
    if (!state->database) {
        db_metadata_free(state->metadata);
        free(state);
        return NULL;
    }
    
    /* Load per-drive databases */
    char drives[26];
    int drive_count = platform_get_available_drives(drives, 26);
    
    for (int i = 0; i < drive_count; i++) {
        char path[MAX_PATH];
        if (!ncd_platform_db_drive_path(drives[i], path, sizeof(path))) {
            continue;
        }
        
        /* Check if file exists */
        FILE *f = fopen(path, "rb");
        if (!f) {
            continue;
        }
        fclose(f);
        
        /* Load this drive's database */
        NcdDatabase *drive_db = db_load_auto(path);
        if (!drive_db) {
            continue;
        }
        
        /* Merge into main database */
        for (int d = 0; d < drive_db->drive_count; d++) {
            DriveData *src = &drive_db->drives[d];
            DriveData *dst = db_add_drive(state->database, src->letter);
            if (!dst) continue;
            
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
        
        db_free(drive_db);
    }
    
    /* Initialize generations */
    state->meta_generation = 1;
    state->db_generation = 1;
    state->last_flush = time(NULL);
    state->last_rescan = state->database->last_scan;
    
    return state;
}

void service_state_cleanup(ServiceState *state) {
    if (!state) {
        return;
    }
    
    /* Flush any pending changes */
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
    
    db_heur_note_choice(state->metadata, search, target);
    state->dirty_flags |= DIRTY_HEURISTICS;
    state->mutation_count++;
    
    return true;
}

bool service_state_add_group(ServiceState *state,
                              const char *name,
                              const char *path) {
    if (!state || !state->metadata || !name || !path) {
        return false;
    }
    
    bool result = db_group_set(state->metadata, name, path);
    if (result) {
        state->dirty_flags |= DIRTY_GROUPS;
        state->mutation_count++;
    }
    
    return result;
}

bool service_state_remove_group(ServiceState *state, const char *name) {
    if (!state || !state->metadata || !name) {
        return false;
    }
    
    bool result = db_group_remove(state->metadata, name);
    if (result) {
        state->dirty_flags |= DIRTY_GROUPS;
        state->mutation_count++;
    }
    
    return result;
}

bool service_state_add_exclusion(ServiceState *state, const char *pattern) {
    if (!state || !state->metadata || !pattern) {
        return false;
    }
    
    bool result = db_exclusion_add(state->metadata, pattern);
    if (result) {
        state->dirty_flags |= DIRTY_EXCLUSIONS;
        state->mutation_count++;
    }
    
    return result;
}

bool service_state_remove_exclusion(ServiceState *state, const char *pattern) {
    if (!state || !state->metadata || !pattern) {
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
        state->dirty_flags |= DIRTY_EXCLUSIONS;
        state->mutation_count++;
    }
    
    return found;
}

bool service_state_update_config(ServiceState *state, const NcdConfig *config) {
    if (!state || !state->metadata || !config) {
        return false;
    }
    
    memcpy(&state->metadata->cfg, config, sizeof(NcdConfig));
    state->dirty_flags |= DIRTY_CONFIG;
    state->mutation_count++;
    
    return true;
}

bool service_state_clear_history(ServiceState *state) {
    if (!state || !state->metadata) {
        return false;
    }
    
    db_heur_clear(state->metadata);
    db_dir_history_clear(state->metadata);
    
    state->dirty_flags |= DIRTY_HEURISTICS | DIRTY_DIR_HISTORY;
    state->mutation_count++;
    
    return true;
}

bool service_state_add_dir_history(ServiceState *state, 
                                    const char *path, 
                                    char drive) {
    if (!state || !state->metadata || !path) {
        return false;
    }
    
    bool result = db_dir_history_add(state->metadata, path, drive);
    if (result) {
        state->dirty_flags |= DIRTY_DIR_HISTORY;
        state->mutation_count++;
    }
    
    return result;
}

bool service_state_update_database(ServiceState *state, NcdDatabase *db) {
    if (!state || !db) {
        return false;
    }
    
    /* Free old database */
    if (state->database) {
        db_free(state->database);
    }
    
    /* Take ownership of new database */
    state->database = db;
    state->last_rescan = time(NULL);
    state->database->last_scan = state->last_rescan;
    
    state->dirty_flags |= DIRTY_DATABASE;
    state->mutation_count++;
    
    return true;
}

/* --------------------------------------------------------- persistence        */

bool service_state_flush(ServiceState *state) {
    if (!state) {
        return false;
    }
    
    if (state->dirty_flags == 0) {
        return true;  /* Nothing to flush */
    }
    
    bool success = true;
    
    /* Save metadata if dirty */
    if (state->dirty_flags & DIRTY_METADATA_ALL) {
        if (!db_metadata_save(state->metadata)) {
            success = false;
        }
    }
    
    /* Save database if dirty */
    if (state->dirty_flags & DIRTY_DATABASE) {
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
    
    if (success) {
        state->dirty_flags = 0;
        state->last_flush = time(NULL);
    }
    
    return success;
}

bool service_state_needs_flush(const ServiceState *state) {
    if (!state) {
        return false;
    }
    return state->dirty_flags != 0;
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
