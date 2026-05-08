"""
Apply P1.4 refactoring: Extract with_metadata() wrapper in src/main.c
"""
import re

with open('src/main.c', 'r', encoding='utf-8') as f:
    content = f.read()

# ============================================================
# EDIT 1: Add typedef + with_metadata() after is_service_backend()
# ============================================================
old1 = '''static bool is_service_backend(void)
{
    return (g_state_view != NULL && g_state_info.from_service);
}

/* ============================================================= directory history */'''

new1 = '''static bool is_service_backend(void)
{
    return (g_state_view != NULL && g_state_info.from_service);
}

/* ============================================================= with_metadata */

/*
 * MetadataMutator callback type for with_metadata().
 * When called from service backend, meta will be NULL (callee handles IPC).
 * When called from local backend, meta is a loaded metadata object.
 * Returns true on success, false on failure.
 */
typedef bool (*MetadataMutator)(NcdMetadata *meta, void *ctx);

/*
 * with_metadata  --  Unified metadata mutation wrapper.
 *
 * Encapsulates the repeated service-vs-local branching pattern:
 *   - Service backend: calls fn(NULL, ctx) -- callee handles IPC
 *   - Local backend:   loads metadata, calls fn(meta, ctx), saves, frees
 *
 * Returns true on success, false on failure.
 */
static bool with_metadata(MetadataMutator fn, void *ctx)
{
    if (is_service_backend()) {
        return fn(NULL, ctx);
    }
    NcdMetadata *meta = db_metadata_load();
    if (!meta) meta = db_metadata_create();
    if (!meta) return false;
    bool ok = fn(meta, ctx);
    if (ok) {
        db_metadata_save(meta);
    }
    db_metadata_free(meta);
    return ok;
}

/* ---- with_metadata callbacks ---- */

/* Exclusion add context */
struct excl_add_ctx {
    const char *pattern;
    const char *err_detail;
};

static bool do_exclusion_remove(NcdMetadata *meta, void *ctx)
{
    const char *pattern = (const char *)ctx;
    if (!meta) {
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_EXCLUSION_REMOVE, pattern, strlen(pattern) + 1);
        return (result == 0);
    }
    return db_exclusion_remove(meta, pattern);
}

static bool do_exclusion_add(NcdMetadata *meta, void *vctx)
{
    struct excl_add_ctx *ctx = (struct excl_add_ctx *)vctx;
    if (!meta) {
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_EXCLUSION_ADD, ctx->pattern, strlen(ctx->pattern) + 1);
        return (result == 0);
    }
    bool ok = db_exclusion_add(meta, ctx->pattern);
    if (!ok) {
        ctx->err_detail = db_get_last_error();
    }
    return ok;
}

static bool do_history_clear(NcdMetadata *meta, void *ctx)
{
    (void)ctx;
    if (!meta) {
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_CLEAR_HISTORY, NULL, 0);
        return (result == 0);
    }
    db_dir_history_clear(meta);
    return true;
}

/* Context for add_dir_to_history */
struct hist_add_ctx {
    const char *cwd;
    char drive;
};

static bool do_add_dir_to_history(NcdMetadata *meta, void *vctx)
{
    struct hist_add_ctx *ctx = (struct hist_add_ctx *)vctx;
    if (!meta) {
        size_t path_len = strlen(ctx->cwd) + 1;
        size_t data_len = path_len + 1;
        char *data = (char *)malloc(data_len);
        if (!data) return false;
        memcpy(data, ctx->cwd, path_len);
        data[path_len] = ctx->drive;
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_DIR_HISTORY_ADD, data, data_len);
        free(data);
        return (result == 0);
    }
    return db_dir_history_add(meta, ctx->cwd, ctx->drive);
}

/* Context for group_remove */
struct group_remove_ctx {
    const char *name;
    const char *cwd;
    bool have_cwd;
    bool removed_path;
    bool removed_group;
    bool not_found;
};

static bool do_group_remove(NcdMetadata *meta, void *vctx)
{
    struct group_remove_ctx *ctx = (struct group_remove_ctx *)vctx;
    if (!meta) {
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_GROUP_REMOVE, ctx->name, strlen(ctx->name) + 1);
        if (result == 0) {
            ctx->removed_group = true;
            return true;
        }
        ctx->not_found = true;
        return false;
    }
    
    /* Check if current directory is in the group */
    if (ctx->have_cwd) {
        bool in_group = false;
        int count = 0;
        for (int i = 0; i < meta->groups.count; i++) {
            if (_stricmp(meta->groups.groups[i].name, ctx->name) == 0) {
                count++;
                if (_stricmp(meta->groups.groups[i].path, ctx->cwd) == 0) {
                    in_group = true;
                }
            }
        }
        if (in_group && count > 1) {
            if (db_group_remove_path(meta, ctx->name, ctx->cwd)) {
                ctx->removed_path = true;
                return true;
            }
            return false;
        }
    }
    
    if (db_group_remove(meta, ctx->name)) {
        ctx->removed_group = true;
        return true;
    }
    ctx->not_found = true;
    return false;
}

/* Context for group_set */
struct group_set_ctx {
    const char *name;
    const char *cwd;
    bool already_in_group;
    int existing_count;
    bool success;
};

static bool do_group_set(NcdMetadata *meta, void *vctx)
{
    struct group_set_ctx *ctx = (struct group_set_ctx *)vctx;
    if (!meta) {
        /* Service: build IPC data and send */
        size_t name_len = strlen(ctx->name) + 1;
        size_t path_len = strlen(ctx->cwd) + 1;
        size_t data_len = 8 + name_len + path_len;
        char *data = (char *)malloc(data_len);
        if (!data) return false;
        *(uint32_t *)data = (uint32_t)name_len;
        *(uint32_t *)(data + 4) = (uint32_t)path_len;
        memcpy(data + 8, ctx->name, name_len);
        memcpy(data + 8 + name_len, ctx->cwd, path_len);
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_GROUP_ADD, data, data_len);
        free(data);
        ctx->success = (result == 0);
        return ctx->success;
    }
    
    /* Count existing entries for this group (before adding) */
    ctx->existing_count = 0;
    for (int i = 0; i < meta->groups.count; i++) {
        if (_stricmp(meta->groups.groups[i].name, ctx->name) == 0) {
            ctx->existing_count++;
            if (_stricmp(meta->groups.groups[i].path, ctx->cwd) == 0) {
                ctx->already_in_group = true;
            }
        }
    }
    
    ctx->success = db_group_set(meta, ctx->name, ctx->cwd);
    return ctx->success;
}

/* Context for history_remove */
struct hist_remove_ctx {
    int idx;
    const char *entry_path;  /* set by service-path pre-check, or NULL to fetch from meta */
    bool entry_valid;
    int entry_count;
};

static bool do_history_remove(NcdMetadata *meta, void *vctx)
{
    struct hist_remove_ctx *ctx = (struct hist_remove_ctx *)vctx;
    if (!meta) {
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_DIR_HISTORY_REMOVE, &ctx->idx, sizeof(ctx->idx));
        return (result == 0);
    }
    /* Local: validate and remove */
    if (!meta) return false;
    const NcdDirHistoryEntry *entry = db_dir_history_get(meta, ctx->idx);
    if (!entry) {
        ctx->entry_valid = false;
        ctx->entry_count = db_dir_history_count(meta);
        return false;
    }
    ctx->entry_valid = true;
    ctx->entry_path = entry->path;
    db_dir_history_remove(meta, ctx->idx);
    return true;
}

/* Context for history swap */
static bool do_history_swap(NcdMetadata *meta, void *ctx)
{
    (void)ctx;
    if (!meta) {
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_DIR_HISTORY_SWAP, NULL, 0);
        return (result == 0);
    }
    db_dir_history_swap_first_two(meta);
    return true;
}

/* Context for config save */
static bool do_config_save(NcdMetadata *meta, void *ctx)
{
    (void)ctx;
    if (!meta) {
        /* Service: can't run UI on service metadata; caller handles this case
         * by NOT using with_metadata for the service path of config_edit. */
        return false;
    }
    /* Only used for local path - ui_edit_config is called before */
    return true;
}

/* ============================================================= directory history */'''

assert old1 in content, "EDIT 1: old string not found!"
content = content.replace(old1, new1, 1)
print("EDIT 1 applied successfully")


# ============================================================
# EDIT 2: Refactor add_current_dir_to_history()
# ============================================================
old2 = '''/*
 * Add current working directory to the directory history.
 * Called whenever NCD successfully navigates to a directory.
 */
static void add_current_dir_to_history(void)
{
    char cwd[MAX_PATH] = {0};
    if (!platform_get_current_dir(cwd, sizeof(cwd))) return;
    
    char drive = platform_get_drive_letter(cwd);
    if (drive == 0) {
        drive = cwd[0];
    }
    
    if (is_service_backend()) {
        /* Service mode: send update via IPC
         * Data format: [path string (null-terminated)][drive byte]
         * We send the actual string bytes, not a pointer! */
        size_t path_len = strlen(cwd) + 1;  /* Include null terminator */
        size_t data_len = path_len + 1;     /* path + drive byte */
        char *data = (char *)malloc(data_len);
        if (!data) return;
        memcpy(data, cwd, path_len);
        data[path_len] = drive;  /* Drive byte after null terminator */
        state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_DIR_HISTORY_ADD, 
                                             data, data_len);
        free(data);
    } else {
        /* Standalone mode: load, modify, save */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        if (!meta) return;
        
        if (db_dir_history_add(meta, cwd, drive)) {
            meta->dir_history_dirty = true;
            db_metadata_save(meta);
        }
        db_metadata_free(meta);
    }
}'''

new2 = '''/*
 * Add current working directory to the directory history.
 * Called whenever NCD successfully navigates to a directory.
 */
static void add_current_dir_to_history(void)
{
    char cwd[MAX_PATH] = {0};
    if (!platform_get_current_dir(cwd, sizeof(cwd))) return;
    
    char drive = platform_get_drive_letter(cwd);
    if (drive == 0) {
        drive = cwd[0];
    }
    
    struct hist_add_ctx ctx = { cwd, drive };
    with_metadata(do_add_dir_to_history, &ctx);
}'''

assert old2 in content, "EDIT 2: old string not found!"
content = content.replace(old2, new2, 1)
print("EDIT 2 applied successfully")


# ============================================================
# EDIT 3: Refactor group_remove
# ============================================================
old3 = '''    /* ----------------------------------------------- group remove command */
    if (opts.group_remove) {
        char cwd[MAX_PATH] = {0};
        bool have_cwd = platform_get_current_dir(cwd, sizeof(cwd));
        
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            /* Note: Service handles both single-path removal and full group removal */
            int result = state_backend_submit_metadata_update(g_state_view, 
                NCD_META_UPDATE_GROUP_REMOVE, opts.group_name, 
                strlen(opts.group_name) + 1);
            if (result == 0) {
                ncd_printf("Group '%s' removed.\\r\\n", opts.group_name);
            } else {
                ncd_printf("Group '%s' not found or could not be removed.\\r\\n", opts.group_name);
            }
            con_close();
            return 0;
        }
        
        /* Standalone mode: load, modify, save */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        /* Check if current directory is in the group */
        bool in_group = false;
        if (have_cwd) {
            int count = 0;
            for (int i = 0; i < meta->groups.count; i++) {
                if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0) {
                    count++;
                    if (_stricmp(meta->groups.groups[i].path, cwd) == 0) {
                        in_group = true;
                    }
                }
            }
            
            if (in_group && count > 1) {
                /* Remove only current directory from group */
                if (db_group_remove_path(meta, opts.group_name, cwd)) {
                    db_metadata_save(meta);
                    ncd_printf("Removed '%s' from group '%s'.\\r\\n", cwd, opts.group_name);
                } else {
                    ncd_printf("Failed to remove from group '%s'.\\r\\n", opts.group_name);
                }
                db_metadata_free(meta);
                con_close();
                return 0;
            }
        }
        
        /* Remove entire group (all entries) */
        if (db_group_remove(meta, opts.group_name)) {
            db_metadata_save(meta);
            ncd_printf("Group '%s' removed.\\r\\n", opts.group_name);
        } else {
            ncd_printf("Group '%s' not found.\\r\\n", opts.group_name);
        }
        db_metadata_free(meta);
        con_close();
        return 0;
    }'''

new3 = '''    /* ----------------------------------------------- group remove command */
    if (opts.group_remove) {
        char cwd[MAX_PATH] = {0};
        bool have_cwd = platform_get_current_dir(cwd, sizeof(cwd));
        
        struct group_remove_ctx ctx = { opts.group_name, cwd, have_cwd, false, false, false };
        bool ok = with_metadata(do_group_remove, &ctx);
        
        if (ok) {
            if (ctx.removed_path) {
                ncd_printf("Removed '%s' from group '%s'.\\r\\n", cwd, opts.group_name);
            } else {
                ncd_printf("Group '%s' removed.\\r\\n", opts.group_name);
            }
        } else {
            if (ctx.not_found) {
                ncd_printf("Group '%s' not found.\\r\\n", opts.group_name);
            } else {
                ncd_printf("Failed to remove from group '%s'.\\r\\n", opts.group_name);
            }
        }
        con_close();
        return 0;
    }'''

assert old3 in content, "EDIT 3: old string not found!"
content = content.replace(old3, new3, 1)
print("EDIT 3 applied successfully")


# ============================================================
# EDIT 4: Refactor group_set
# ============================================================
old4 = '''    /* ------------------------------------------------ group set command   */
    if (opts.group_set) {
        char cwd[MAX_PATH] = {0};
        if (!platform_get_current_dir(cwd, sizeof(cwd))) {
            result_error("Could not determine current directory.");
            con_close();
            return 1;
        }
        
        if (is_service_backend()) {
            /* Service mode: use state view to check current status, then send update */
            const NcdMetadata *meta = get_state_metadata();
            
            /* Check if already in group before adding */
            bool already_in_group = false;
            int existing_count = 0;
            if (meta) {
                for (int i = 0; i < meta->groups.count; i++) {
                    if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0) {
                        existing_count++;
                        if (_stricmp(meta->groups.groups[i].path, cwd) == 0) {
                            already_in_group = true;
                        }
                    }
                }
            }
            
            /* Send update via IPC
             * Data format: [name_len (4 bytes)][path_len (4 bytes)][name string][path string]
             * We send the actual string bytes, not pointers! */
            size_t name_len = strlen(opts.group_name) + 1;  /* Include null terminator */
            size_t path_len = strlen(cwd) + 1;
            size_t data_len = 8 + name_len + path_len;  /* 2 x uint32_t + both strings */
            char *data = (char *)malloc(data_len);
            if (!data) {
                ncd_println("Failed to set group (out of memory?).");
                con_close();
                return 1;
            }
            *(uint32_t *)data = (uint32_t)name_len;
            *(uint32_t *)(data + 4) = (uint32_t)path_len;
            memcpy(data + 8, opts.group_name, name_len);
            memcpy(data + 8 + name_len, cwd, path_len);
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_GROUP_ADD, data, data_len);
            free(data);
            
            if (result == 0) {
                if (already_in_group) {
                    ncd_printf("'%s' is already in group '%s'.\\r\\n", cwd, opts.group_name);
                } else if (existing_count > 0) {
                    ncd_printf("Added to group '%s' (%d entries) -> '%s'\\r\\n", 
                               opts.group_name, existing_count + 1, cwd);
                } else {
                    ncd_printf("Group '%s' -> '%s'\\r\\n", opts.group_name, cwd);
                }
            } else {
                ncd_println("Failed to set group (too many groups?).");
            }
            con_close();
            return 0;
        }
        
        /* Standalone mode: load, modify, save */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        /* Check if already in group before adding */
        bool already_in_group = false;
        for (int i = 0; i < meta->groups.count; i++) {
            if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0 &&
                _stricmp(meta->groups.groups[i].path, cwd) == 0) {
                already_in_group = true;
                break;
            }
        }
        
        /* Count existing entries for this group (before adding) */
        int existing_count = 0;
        for (int i = 0; i < meta->groups.count; i++) {
            if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0) {
                existing_count++;
            }
        }
        
        if (db_group_set(meta, opts.group_name, cwd)) {
            db_metadata_save(meta);
            
            if (already_in_group) {
                ncd_printf("'%s' is already in group '%s'.\\r\\n", cwd, opts.group_name);
            } else if (existing_count > 0) {
                /* Added to existing group */
                ncd_printf("Added to group '%s' (%d entries) -> '%s'\\r\\n", 
                           opts.group_name, existing_count + 1, cwd);
            } else {
                /* New group created */
                ncd_printf("Group '%s' -> '%s'\\r\\n", opts.group_name, cwd);
            }
        } else {
            ncd_println("Failed to set group (too many groups?).");
        }
        db_metadata_free(meta);
        con_close();
        return 0;
    }'''

new4 = '''    /* ------------------------------------------------ group set command   */
    if (opts.group_set) {
        char cwd[MAX_PATH] = {0};
        if (!platform_get_current_dir(cwd, sizeof(cwd))) {
            result_error("Could not determine current directory.");
            con_close();
            return 1;
        }
        
        struct group_set_ctx ctx = { opts.group_name, cwd, false, 0, false };
        
        if (is_service_backend()) {
            /* Service: pre-check state before IPC */
            const NcdMetadata *smeta = get_state_metadata();
            if (smeta) {
                for (int i = 0; i < smeta->groups.count; i++) {
                    if (_stricmp(smeta->groups.groups[i].name, opts.group_name) == 0) {
                        ctx.existing_count++;
                        if (_stricmp(smeta->groups.groups[i].path, cwd) == 0) {
                            ctx.already_in_group = true;
                        }
                    }
                }
            }
        }
        
        bool ok = with_metadata(do_group_set, &ctx);
        
        if (ok) {
            if (ctx.already_in_group) {
                ncd_printf("'%s' is already in group '%s'.\\r\\n", cwd, opts.group_name);
            } else if (ctx.existing_count > 0) {
                ncd_printf("Added to group '%s' (%d entries) -> '%s'\\r\\n", 
                           opts.group_name, ctx.existing_count + 1, cwd);
            } else {
                ncd_printf("Group '%s' -> '%s'\\r\\n", opts.group_name, cwd);
            }
        } else {
            ncd_println("Failed to set group (too many groups?).");
        }
        con_close();
        return 0;
    }'''

assert old4 in content, "EDIT 4: old string not found!"
content = content.replace(old4, new4, 1)
print("EDIT 4 applied successfully")


# ============================================================
# EDIT 5: Refactor exclusion_remove
# ============================================================
old5 = '''    /* ------------------------------------------------ exclusion remove command */
    if (opts.exclusion_remove) {
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_EXCLUSION_REMOVE, opts.exclusion_pattern,
                strlen(opts.exclusion_pattern) + 1);
            if (result == 0) {
                ncd_printf("Removed exclusion: %s\\r\\n", opts.exclusion_pattern);
            } else {
                ncd_printf("Exclusion not found: %s\\r\\n", opts.exclusion_pattern);
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (!meta) meta = db_metadata_create();
            
            if (db_exclusion_remove(meta, opts.exclusion_pattern)) {
                db_metadata_save(meta);
                ncd_printf("Removed exclusion: %s\\r\\n", opts.exclusion_pattern);
            } else {
                ncd_printf("Exclusion not found: %s\\r\\n", opts.exclusion_pattern);
            }
            db_metadata_free(meta);
        }
        con_close();
        return 0;
    }'''

new5 = '''    /* ------------------------------------------------ exclusion remove command */
    if (opts.exclusion_remove) {
        bool ok = with_metadata(do_exclusion_remove, (void *)opts.exclusion_pattern);
        if (ok) {
            ncd_printf("Removed exclusion: %s\\r\\n", opts.exclusion_pattern);
        } else {
            ncd_printf("Exclusion not found: %s\\r\\n", opts.exclusion_pattern);
        }
        con_close();
        return 0;
    }'''

assert old5 in content, "EDIT 5: old string not found!"
content = content.replace(old5, new5, 1)
print("EDIT 5 applied successfully")


# ============================================================
# EDIT 6: Refactor exclusion_add
# ============================================================
old6 = '''    /* ------------------------------------------------ exclusion add command */
    if (opts.exclusion_add) {
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_EXCLUSION_ADD, opts.exclusion_pattern,
                strlen(opts.exclusion_pattern) + 1);
            if (result == 0) {
                ncd_printf("Added exclusion: %s\\r\\n", opts.exclusion_pattern);
            } else {
                ncd_printf("Failed to add exclusion.\\r\\n");
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (!meta) meta = db_metadata_create();
            
            if (db_exclusion_add(meta, opts.exclusion_pattern)) {
                db_metadata_save(meta);
                ncd_printf("Added exclusion: %s\\r\\n", opts.exclusion_pattern);
            } else {
                const char *err = db_get_last_error();
                ncd_printf("Failed to add exclusion: %s\\r\\n", err);
            }
            db_metadata_free(meta);
        }
        con_close();
        return 0;
    }'''

new6 = '''    /* ------------------------------------------------ exclusion add command */
    if (opts.exclusion_add) {
        struct excl_add_ctx ctx = { opts.exclusion_pattern, NULL };
        bool ok = with_metadata(do_exclusion_add, &ctx);
        if (ok) {
            ncd_printf("Added exclusion: %s\\r\\n", opts.exclusion_pattern);
        } else if (ctx.err_detail) {
            ncd_printf("Failed to add exclusion: %s\\r\\n", ctx.err_detail);
        } else {
            ncd_printf("Failed to add exclusion.\\r\\n");
        }
        con_close();
        return 0;
    }'''

assert old6 in content, "EDIT 6: old string not found!"
content = content.replace(old6, new6, 1)
print("EDIT 6 applied successfully")


# ============================================================
# EDIT 7: Refactor history_clear
# ============================================================
old7 = '''    /* ------------------------------------------------ directory history clear */
    if (opts.history_clear) {
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_CLEAR_HISTORY, NULL, 0);
            if (result == 0) {
                ncd_println("Directory history cleared.");
            } else {
                ncd_println("Failed to clear directory history.");
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (meta) {
                db_dir_history_clear(meta);
                db_metadata_save(meta);
                ncd_println("Directory history cleared.");
                db_metadata_free(meta);
            }
        }
        con_close();
        return 0;
    }'''

new7 = '''    /* ------------------------------------------------ directory history clear */
    if (opts.history_clear) {
        bool ok = with_metadata(do_history_clear, NULL);
        if (ok) {
            ncd_println("Directory history cleared.");
        } else {
            ncd_println("Failed to clear directory history.");
        }
        con_close();
        return 0;
    }'''

assert old7 in content, "EDIT 7: old string not found!"
content = content.replace(old7, new7, 1)
print("EDIT 7 applied successfully")


# ============================================================
# EDIT 8: Refactor history_remove
# ============================================================
old8 = '''    /* ------------------------------------------------ directory history remove by index */
    if (opts.history_remove > 0) {
        int idx = opts.history_remove - 1;  /* convert 1-based to 0-based */
        
        if (is_service_backend()) {
            /* Service mode: read from state view, then send update via IPC */
            const NcdMetadata *meta = get_state_metadata();
            if (!meta) {
                ncd_println("No history.");
                con_close();
                return 1;
            }
            
            const NcdDirHistoryEntry *entry = db_dir_history_get(meta, idx);
            if (!entry) {
                ncd_printf("History entry %d not found (only %d entries).\\r\\n",
                           opts.history_remove, db_dir_history_count(meta));
                con_close();
                return 1;
            }
            
            ncd_printf("Removed from history: %s\\r\\n", entry->path);
            
            /* Send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_DIR_HISTORY_REMOVE, &idx, sizeof(idx));
            if (result != 0) {
                ncd_println("Warning: Failed to update service.");
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (!meta) {
                ncd_println("No history.");
                con_close();
                return 1;
            }
            
            const NcdDirHistoryEntry *entry = db_dir_history_get(meta, idx);
            if (!entry) {
                ncd_printf("History entry %d not found (only %d entries).\\r\\n",
                           opts.history_remove, db_dir_history_count(meta));
                db_metadata_free(meta);
                con_close();
                return 1;
            }
            
            ncd_printf("Removed from history: %s\\r\\n", entry->path);
            db_dir_history_remove(meta, idx);
            db_metadata_save(meta);
            db_metadata_free(meta);
        }
        con_close();
        return 0;
    }'''

new8 = '''    /* ------------------------------------------------ directory history remove by index */
    if (opts.history_remove > 0) {
        int idx = opts.history_remove - 1;  /* convert 1-based to 0-based */
        
        /* Pre-validate: check entry exists and get its path for printing */
        const char *entry_path = NULL;
        int entry_count = 0;
        
        if (is_service_backend()) {
            const NcdMetadata *smeta = get_state_metadata();
            if (!smeta) {
                ncd_println("No history.");
                con_close();
                return 1;
            }
            const NcdDirHistoryEntry *entry = db_dir_history_get(smeta, idx);
            if (!entry) {
                ncd_printf("History entry %d not found (only %d entries).\\r\\n",
                           opts.history_remove, db_dir_history_count(smeta));
                con_close();
                return 1;
            }
            entry_path = entry->path;
        } else {
            NcdMetadata *meta = db_metadata_load();
            if (!meta) {
                ncd_println("No history.");
                con_close();
                return 1;
            }
            const NcdDirHistoryEntry *entry = db_dir_history_get(meta, idx);
            entry_count = db_dir_history_count(meta);
            if (!entry) {
                ncd_printf("History entry %d not found (only %d entries).\\r\\n",
                           opts.history_remove, entry_count);
                db_metadata_free(meta);
                con_close();
                return 1;
            }
            entry_path = entry->path;
            db_metadata_free(meta);
        }
        
        ncd_printf("Removed from history: %s\\r\\n", entry_path);
        
        /* Perform the actual removal */
        struct hist_remove_ctx ctx = { idx, NULL, true, 0 };
        bool ok = with_metadata(do_history_remove, &ctx);
        if (!ok && is_service_backend()) {
            ncd_println("Warning: Failed to update service.");
        }
        con_close();
        return 0;
    }'''

assert old8 in content, "EDIT 8: old string not found!"
content = content.replace(old8, new8, 1)
print("EDIT 8 applied successfully")


# ============================================================
# EDIT 9: Refactor history_pingpong (swap part only)
# ============================================================
old9 = '''        if (is_service_backend()) {
            /* Service mode: send swap update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_DIR_HISTORY_SWAP, NULL, 0);
            if (result != 0) {
                ncd_println("Warning: Failed to update service.");
            }
        } else {
            /* Standalone mode: swap and save */
            NcdMetadata *meta = (NcdMetadata *)meta_view;
            db_dir_history_swap_first_two(meta);
            meta->dir_history_dirty = true;
            db_metadata_save(meta);
            db_metadata_free(meta);
        }'''

new9 = '''        if (is_service_backend()) {
            bool ok = with_metadata(do_history_swap, NULL);
            if (!ok) {
                ncd_println("Warning: Failed to update service.");
            }
        } else {
            NcdMetadata *meta = (NcdMetadata *)meta_view;
            db_dir_history_swap_first_two(meta);
            meta->dir_history_dirty = true;
            db_metadata_save(meta);
            db_metadata_free(meta);
        }'''

assert old9 in content, "EDIT 9: old string not found!"
content = content.replace(old9, new9, 1)
print("EDIT 9 applied successfully")


# ============================================================
# EDIT 10: Refactor first_run_config
# ============================================================
old10 = '''            if (is_service_backend()) {
                /* Service mode: send update via IPC */
                if (state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_CONFIG,
                                                         &meta->cfg, sizeof(meta->cfg)) == 0) {
                    ncd_println("Configuration saved.\\r\\n");
                } else {
                    ncd_println("Warning: Could not save configuration via service.\\r\\n");
                }
            } else {
                /* Standalone mode: save directly */
                if (db_metadata_save(meta)) {
                    ncd_println("Configuration saved.\\r\\n");
                } else {
                    ncd_println("Warning: Could not save configuration.\\r\\n");
                }
            }'''

new10 = '''            if (is_service_backend()) {
                /* Service mode: send update via IPC */
                if (state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_CONFIG,
                                                         &meta->cfg, sizeof(meta->cfg)) == 0) {
                    ncd_println("Configuration saved.\\r\\n");
                } else {
                    ncd_println("Warning: Could not save configuration via service.\\r\\n");
                }
            } else {
                /* Standalone mode: save directly */
                bool ok = with_metadata(do_config_save, NULL);
                /* with_metadata saves on success; meta was already populated */
                if (ok) {
                    /* For first-run, meta was already created+populated; use db_metadata_save directly */
                    db_metadata_save(meta);
                    ncd_println("Configuration saved.\\r\\n");
                } else {
                    ncd_println("Warning: Could not save configuration.\\r\\n");
                }
            }'''

assert old10 in content, "EDIT 10: old string not found!"
content = content.replace(old10, new10, 1)
print("EDIT 10 applied successfully")


# ============================================================
# EDIT 11: Refactor config_edit
# ============================================================
old11 = '''            if (is_service_backend()) {
                /* Service mode: send update via IPC */
                if (state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_CONFIG,
                                                         &meta->cfg, sizeof(meta->cfg)) == 0) {
                    ncd_println("Configuration saved.");
                } else {
                    ncd_println("Failed to save configuration via service.");
                }
            } else {
                /* Standalone mode: save directly */
                if (db_metadata_save(meta)) {
                    ncd_println("Configuration saved.");
                } else {
                    ncd_println("Failed to save configuration.");
                }
            }'''

new11 = '''            if (is_service_backend()) {
                /* Service mode: send update via IPC */
                if (state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_CONFIG,
                                                         &meta->cfg, sizeof(meta->cfg)) == 0) {
                    ncd_println("Configuration saved.");
                } else {
                    ncd_println("Failed to save configuration via service.");
                }
            } else {
                /* Standalone mode: save directly (meta is from get_metadata()) */
                if (db_metadata_save(meta)) {
                    ncd_println("Configuration saved.");
                } else {
                    ncd_println("Failed to save configuration.");
                }
            }'''

assert old11 in content, "EDIT 11: old string not found!"
content = content.replace(old11, new11, 1)
print("EDIT 11 applied successfully")


# ============================================================
# Write the result
# ============================================================
with open('src/main.c', 'w', encoding='utf-8') as f:
    f.write(content)

print("\nAll edits applied. File written successfully.")
print(f"Total lines: {len(content.splitlines())}")
