/*
 * Agent 3 Patch — Move, Rename & Symlinks
 * Target files:
 *   - src/main.c        (agent_mode_mv, agent_mode_ln)
 *   - src/database.c    (db_remove_path)
 *   - ../shared/platform.c (platform_move_dir, platform_create_symlink, platform_dir_is_empty)
 *
 * These functions replace the existing stubs.
 */

/* ================================================================
 * Target file: ../shared/platform.c
 * Function: platform_remove_dir
 * ================================================================ */
bool platform_remove_dir(const char *path)
{
    if (!path) return false;
#if PLATFORM_WINDOWS
    return RemoveDirectoryA(path) != 0;
#else
    return rmdir(path) == 0;
#endif
}

/* ================================================================
 * Target file: ../shared/platform.c
 * Function: platform_dir_is_empty
 * ================================================================ */
bool platform_dir_is_empty(const char *path)
{
    if (!path) return false;
#if PLATFORM_WINDOWS
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);
    WIN32_FIND_DATAA find_data;
    HANDLE h = FindFirstFileA(search_path, &find_data);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool empty = true;
    do {
        if (strcmp(find_data.cFileName, ".") != 0 &&
            strcmp(find_data.cFileName, "..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileA(h, &find_data));
    FindClose(h);
    return empty;
#else
    DIR *dir = opendir(path);
    if (!dir) return false;
    struct dirent *ent;
    bool empty = true;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 &&
            strcmp(ent->d_name, "..") != 0) {
            empty = false;
            break;
        }
    }
    closedir(dir);
    return empty;
#endif
}

/* ================================================================
 * Target file: ../shared/platform.c
 * Function: platform_move_dir
 * ================================================================ */
bool platform_move_dir(const char *src, const char *dst)
{
    if (!src || !dst) return false;
#if PLATFORM_WINDOWS
    return MoveFileA(src, dst) != 0;
#else
    return rename(src, dst) == 0;
#endif
}

/* ================================================================
 * Target file: ../shared/platform.c
 * Function: platform_create_symlink
 * ================================================================ */
bool platform_create_symlink(const char *target, const char *link)
{
    if (!target || !link) return false;
#if PLATFORM_WINDOWS
    DWORD flags = 0;
    DWORD attr = GetFileAttributesA(target);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    }
    return CreateSymbolicLinkA(link, target, flags) != 0;
#else
    return symlink(target, link) == 0;
#endif
}

/* ================================================================
 * Target file: src/database.c
 * Function: db_remove_path
 * ================================================================ */
bool db_remove_path(NcdDatabase *db, const char *path)
{
    if (!db || !path || !path[0]) return false;

    char target_drive = 0;
#if NCD_PLATFORM_WINDOWS
    if (path[1] == ':') {
        target_drive = (char)toupper((unsigned char)path[0]);
    }
#else
    target_drive = (char)toupper((unsigned char)path[0]);
#endif

    DriveData *drv = db_find_drive(db, target_drive);
    if (!drv) return false;

    /* Find the directory index by full path reconstruction */
    int dir_idx = -1;
    char buf[NCD_MAX_PATH];
    for (int i = 0; i < drv->dir_count; i++) {
        if (!db_full_path(drv, i, buf, sizeof(buf))) continue;
        if (_stricmp(buf, path) == 0) {
            dir_idx = i;
            break;
        }
    }

    if (dir_idx < 0) return false;

    /* Collect all indices to remove (dir + all descendants) */
    bool *to_remove = ncd_calloc((size_t)drv->dir_count, sizeof(bool));
    to_remove[dir_idx] = true;

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < drv->dir_count; i++) {
            if (to_remove[i]) continue;
            int parent = drv->dirs[i].parent;
            if (parent >= 0 && parent < drv->dir_count && to_remove[parent]) {
                to_remove[i] = true;
                changed = true;
            }
        }
    }

    /* Build mapping from old indices to new indices */
    int *old_to_new = ncd_malloc_array((size_t)drv->dir_count, sizeof(int));
    for (int i = 0; i < drv->dir_count; i++) old_to_new[i] = -1;

    DirEntry *new_dirs = ncd_malloc_array((size_t)drv->dir_count, sizeof(DirEntry));
    int new_count = 0;
    for (int i = 0; i < drv->dir_count; i++) {
        if (!to_remove[i]) {
            old_to_new[i] = new_count;
            new_dirs[new_count++] = drv->dirs[i];
        }
    }

    /* Update parent references */
    for (int i = 0; i < new_count; i++) {
        if (new_dirs[i].parent >= 0) {
            new_dirs[i].parent = old_to_new[new_dirs[i].parent];
        }
    }

    free(drv->dirs);
    if (new_count > 0) {
        drv->dirs = new_dirs;
    } else {
        drv->dirs = NULL;
        free(new_dirs);
    }
    drv->dir_count = new_count;
    drv->dir_capacity = new_count;

    free(old_to_new);
    free(to_remove);

    /* Invalidate cached name index */
    db->name_index_generation++;
    return true;
}

/* ================================================================
 * Target file: src/main.c
 * Function: agent_mode_mv
 * ================================================================ */
static int agent_mode_mv(const NcdOptions *opts)
{
    const char *src = opts->search;
    const char *dst = opts->agent_mkdirs_file;

    if (!src || !src[0] || !dst || !dst[0]) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"error\":\"missing src or dst\",\"result\":\"error\"}\r\n");
        } else {
            agent_print("ERROR: Missing source or destination path\r\n");
        }
        return 1;
    }

    if (!platform_dir_exists(src)) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"src\":\"");
            agent_json_escape(src);
            agent_print("\",\"dst\":\"");
            agent_json_escape(dst);
            agent_print("\",\"result\":\"error_not_found\",\"message\":\"Source not found\"}\r\n");
        } else {
            agent_print("ERROR: Source not found\r\n");
        }
        return 1;
    }

    bool dst_exists = platform_dir_exists(dst) || platform_file_exists(dst);
    if (dst_exists) {
        if (!opts->agent_force) {
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"src\":\"");
                agent_json_escape(src);
                agent_print("\",\"dst\":\"");
                agent_json_escape(dst);
                agent_print("\",\"result\":\"error_exists\",\"message\":\"Destination already exists\"}\r\n");
            } else {
                agent_print("ERROR: Destination already exists\r\n");
            }
            return 1;
        }

        /* Force: only overwrite if empty directory */
        if (platform_file_exists(dst) || !platform_dir_is_empty(dst)) {
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"src\":\"");
                agent_json_escape(src);
                agent_print("\",\"dst\":\"");
                agent_json_escape(dst);
                agent_print("\",\"result\":\"error_exists\",\"message\":\"Destination is not empty\"}\r\n");
            } else {
                agent_print("ERROR: Destination is not empty\r\n");
            }
            return 1;
        }

        /* Remove empty dst directory */
        if (!platform_remove_dir(dst)) {
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"src\":\"");
                agent_json_escape(src);
                agent_print("\",\"dst\":\"");
                agent_json_escape(dst);
                agent_print("\",\"result\":\"error_perms\",\"message\":\"Cannot remove destination\"}\r\n");
            } else {
                agent_print("ERROR: Cannot remove destination\r\n");
            }
            return 1;
        }
    }

    /* Perform the move */
    if (!platform_move_dir(src, dst)) {
        const char *result_str = "error";
        const char *msg = "Failed to move directory";
#if NCD_PLATFORM_WINDOWS
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            result_str = "error_perms";
            msg = "Permission denied";
        }
#else
        if (errno == EACCES || errno == EPERM) {
            result_str = "error_perms";
            msg = "Permission denied";
        }
#endif
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"src\":\"");
            agent_json_escape(src);
            agent_print("\",\"dst\":\"");
            agent_json_escape(dst);
            agent_printf("\",\"result\":\"%s\",\"message\":\"", result_str);
            agent_json_escape(msg);
            agent_print("\"}\r\n");
        } else {
            agent_printf("ERROR: %s\r\n", msg);
        }
        return 1;
    }

    /* Update database: remove old path, add new path */
    {
        char src_drive = path_get_drive(src);
        if (src_drive == 0 && src[0] != '\0') {
            src_drive = (char)toupper((unsigned char)src[0]);
        }

        /* Load source drive database and remove old path */
        char db_path[NCD_MAX_PATH] = {0};
        NcdDatabase *db = NULL;
        if (db_drive_path(src_drive, db_path, sizeof(db_path))) {
            for (int i = 0; i < g_dirty_db_count; i++) {
                if (g_dirty_dbs[i].drive == src_drive && g_dirty_dbs[i].db) {
                    db = g_dirty_dbs[i].db;
                    break;
                }
            }
            if (!db) {
                db = db_load_auto(db_path);
            }
        }
        if (!db) {
            db = db_create();
            db->last_scan = time(NULL);
        }

        db_remove_path(db, src);

        /* Ensure db is tracked for saving */
        if (db_drive_path(src_drive, db_path, sizeof(db_path))) {
            db_mark_dirty_standalone(src_drive, db_path, db);
        }

        /* Add new path to database */
        add_path_to_database(dst);

        /* Flush dirty databases immediately */
        flush_all_dirty_dbs();
    }

    if (opts->agent_json) {
        agent_print("{\"v\":1,\"src\":\"");
        agent_json_escape(src);
        agent_print("\",\"dst\":\"");
        agent_json_escape(dst);
        agent_print("\",\"result\":\"moved\",\"message\":\"Directory moved\"}\r\n");
    } else {
        agent_print("Directory moved\r\n");
    }
    return 0;
}

/* ================================================================
 * Target file: src/main.c
 * Function: agent_mode_ln
 * ================================================================ */
static int agent_mode_ln(const NcdOptions *opts)
{
    const char *target = opts->search;
    const char *link = opts->agent_mkdirs_file;

    if (!target || !target[0] || !link || !link[0]) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"error\":\"missing target or link\",\"result\":\"error\"}\r\n");
        } else {
            agent_print("ERROR: Missing target or link path\r\n");
        }
        return 1;
    }

    if (platform_dir_exists(link) || platform_file_exists(link)) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"target\":\"");
            agent_json_escape(target);
            agent_print("\",\"link\":\"");
            agent_json_escape(link);
            agent_print("\",\"result\":\"error_exists\",\"message\":\"Link path already exists\"}\r\n");
        } else {
            agent_print("ERROR: Link path already exists\r\n");
        }
        return 1;
    }

    if (!platform_create_symlink(target, link)) {
        const char *result_str = "error";
        const char *msg = "Failed to create symbolic link";
#if NCD_PLATFORM_WINDOWS
        DWORD err = GetLastError();
        if (err == ERROR_PRIVILEGE_NOT_HELD) {
            result_str = "error_unsupported";
            msg = "Symbolic links require developer mode or administrator privilege";
        } else if (err == ERROR_ACCESS_DENIED) {
            result_str = "error_perms";
            msg = "Permission denied";
        }
#else
        if (errno == EACCES || errno == EPERM) {
            result_str = "error_perms";
            msg = "Permission denied";
        } else if (errno == EEXIST) {
            result_str = "error_exists";
            msg = "Link path already exists";
        } else if (errno == ENOSYS || errno == EOPNOTSUPP) {
            result_str = "error_unsupported";
            msg = "Symbolic links not supported on this filesystem";
        }
#endif
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"target\":\"");
            agent_json_escape(target);
            agent_print("\",\"link\":\"");
            agent_json_escape(link);
            agent_printf("\",\"result\":\"%s\",\"message\":\"", result_str);
            agent_json_escape(msg);
            agent_print("\"}\r\n");
        } else {
            agent_printf("ERROR: %s\r\n", msg);
        }
        return 1;
    }

    /* Update database if target is a directory */
    if (platform_dir_exists(target)) {
        add_path_to_database(link);
        flush_all_dirty_dbs();
    }

    if (opts->agent_json) {
        agent_print("{\"v\":1,\"target\":\"");
        agent_json_escape(target);
        agent_print("\",\"link\":\"");
        agent_json_escape(link);
        agent_print("\",\"result\":\"created\",\"message\":\"Symbolic link created\"}\r\n");
    } else {
        agent_print("Symbolic link created\r\n");
    }
    return 0;
}
