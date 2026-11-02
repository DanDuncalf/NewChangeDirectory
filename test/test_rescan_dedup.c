/* test_rescan_dedup.c -- append-only db_add_dir, path index, hashed rescans */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/scanner.h"
#include "../src/ncd.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#if NCD_PLATFORM_WINDOWS
#include <direct.h>
#define TRD_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TRD_MKDIR(p) mkdir(p, 0755)
#endif

TEST(add_dir_is_append_only) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int u = db_add_dir(drv, "Users", -1, false, false);
    int a = db_add_dir(drv, "Docs", u, false, false);
    int b = db_add_dir(drv, "Docs", u, false, false);
    int c = db_add_dir(drv, "docs", u, false, false);
    ASSERT_EQ_INT(1, a);
    ASSERT_EQ_INT(2, b);
    ASSERT_EQ_INT(3, c);
    ASSERT_EQ_INT(4, drv->dir_count);
    db_free(db);
    return 0;
}

TEST(add_dir_300k_appends_fast) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    char name[32];
    clock_t t0 = clock();
    for (int i = 0; i < 300000; i++) {
        snprintf(name, sizeof(name), "n%07d", i);
        db_add_dir(drv, name, i < 100 ? -1 : (i / 100) - 1, false, false);
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  300K appends: %.3f s\n", secs);
    ASSERT_EQ_INT(300000, drv->dir_count);
    ASSERT_TRUE(secs < 0.5);   /* old linear dedup: ~30 s */
    db_free(db);
    return 0;
}

TEST(path_index_finds_every_entry) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    int b = db_add_dir(drv, "Beta", r, false, false);
    db_add_dir(drv, "Gamma", b, false, false);
    db_add_dir(drv, "Delta", r, false, false);
    char p[NCD_MAX_PATH];
    for (int i = 0; i < drv->dir_count; i++) {
        ASSERT_NOT_NULL(db_full_path(drv, i, p, sizeof(p)));
        ASSERT_EQ_INT(i, db_path_index_get(db, p));
    }
    ASSERT_EQ_INT(-1, db_path_index_get(db, "C:\\Alpha\\Nope"));
    db_free(db);
    return 0;
}

TEST(path_index_case_rules) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    db_add_dir(drv, "Beta", r, false, false);
    char p[NCD_MAX_PATH];
    db_full_path(drv, 1, p, sizeof(p));
    for (char *q = p; *q; q++) *q = (char)toupper((unsigned char)*q);
#if NCD_PLATFORM_WINDOWS
    ASSERT_EQ_INT(1, db_path_index_get(db, p));
#else
    ASSERT_EQ_INT(-1, db_path_index_get(db, p));
#endif
    db_free(db);
    return 0;
}

TEST(path_index_rebuilt_after_subtree_removal) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    int b = db_add_dir(drv, "Beta", r, false, false);
    db_add_dir(drv, "Gamma", b, false, false);
    int d = db_add_dir(drv, "Delta", r, false, false);
    char pd[NCD_MAX_PATH]; db_full_path(drv, d, pd, sizeof(pd));
    ASSERT_EQ_INT(d, db_path_index_get(db, pd));
    ASSERT_TRUE(db_drive_remove_subtree(db, drv, b));
    ASSERT_EQ_INT(1, db_path_index_get(db, pd));   /* Delta moved from 3 to 1 */
    db_free(db);
    return 0;
}

TEST(children_index_lists_children) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    db_add_dir(drv, "Beta", r, false, false);
    db_add_dir(drv, "Gamma", r, false, false);
    DirChildren *ch = db_build_children_index(drv);
    ASSERT_EQ_INT(2, ch[r].child_count);
    ASSERT_EQ_INT(1, ch[r].children[0]);
    ASSERT_EQ_INT(2, ch[r].children[1]);
    db_free_children_index(ch, drv->dir_count);
    db_free(db);
    return 0;
}

/* --- real filesystem rescans ------------------------------------------ */

static char g_root[NCD_MAX_PATH];

static void mk(const char *rel)
{
    char p[NCD_MAX_PATH];
    snprintf(p, sizeof(p), "%s%s%s", g_root, NCD_PATH_SEP, rel);
    TRD_MKDIR(p);
}

static void fs_setup(void)
{
    char cwd[NCD_MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));
    snprintf(g_root, sizeof(g_root), "%s%strd_tree", cwd, NCD_PATH_SEP);
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", g_root);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
#endif
    system(cmd);
    TRD_MKDIR(g_root);
    mk("sub");
    mk("sub" NCD_PATH_SEP "a");
    mk("sub" NCD_PATH_SEP "b");
    mk("other");
}

static int count_name(const DriveData *drv, const char *name)
{
    int n = 0;
    for (int i = 0; i < drv->dir_count; i++)
        if (strcmp(drv->name_pool + drv->dirs[i].name_off, name) == 0) n++;
    return n;
}

TEST(subdir_rescan_twice_has_no_duplicates) {
    fs_setup();
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(g_root);
    ASSERT_TRUE(scan_subdirectory(db, letter, g_root, true, true, NULL, NULL, NULL) >= 0);
    char sub[NCD_MAX_PATH];
    snprintf(sub, sizeof(sub), "%s%ssub", g_root, NCD_PATH_SEP);
    ASSERT_TRUE(scan_subdirectory(db, letter, sub, true, true, NULL, NULL, NULL) >= 0);
    ASSERT_TRUE(scan_subdirectory(db, letter, sub, true, true, NULL, NULL, NULL) >= 0);
    DriveData *drv = db_find_drive(db, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(1, count_name(drv, "sub"));
    ASSERT_EQ_INT(1, count_name(drv, "a"));
    ASSERT_EQ_INT(1, count_name(drv, "b"));
    ASSERT_EQ_INT(1, count_name(drv, "other"));
    /* parent of "sub" is still the tree root entry */
    int s = db_path_index_get(db, sub);
    ASSERT_TRUE(s >= 0);
    char p[NCD_MAX_PATH];
    db_full_path(drv, s, p, sizeof(p));
    ASSERT_EQ_STR(sub, p);
    db_free(db);
    return 0;
}

TEST(subdir_rescan_picks_up_new_and_removed_dirs) {
    fs_setup();
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(g_root);
    scan_subdirectory(db, letter, g_root, true, true, NULL, NULL, NULL);
    mk("sub" NCD_PATH_SEP "c_new");
    char gone[NCD_MAX_PATH];
    snprintf(gone, sizeof(gone), "%s%ssub%sb", g_root, NCD_PATH_SEP, NCD_PATH_SEP);
#if NCD_PLATFORM_WINDOWS
    _rmdir(gone);
#else
    rmdir(gone);
#endif
    char sub[NCD_MAX_PATH];
    snprintf(sub, sizeof(sub), "%s%ssub", g_root, NCD_PATH_SEP);
    scan_subdirectory(db, letter, sub, true, true, NULL, NULL, NULL);
    DriveData *drv = db_find_drive(db, letter);
    ASSERT_EQ_INT(1, count_name(drv, "c_new"));
    ASSERT_EQ_INT(0, count_name(drv, "b"));
    ASSERT_EQ_INT(1, count_name(drv, "sub"));
    db_free(db);
    return 0;
}

#if NCD_PLATFORM_WINDOWS
TEST(subdir_rescan_with_different_case_replaces_subtree) {
    fs_setup();
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(g_root);
    scan_subdirectory(db, letter, g_root, true, true, NULL, NULL, NULL);
    char sub_upper[NCD_MAX_PATH];
    snprintf(sub_upper, sizeof(sub_upper), "%s\\SUB", g_root);
    scan_subdirectory(db, letter, sub_upper, true, true, NULL, NULL, NULL);
    DriveData *drv = db_find_drive(db, letter);
    int subs = count_name(drv, "sub") + count_name(drv, "SUB");
    ASSERT_EQ_INT(1, subs);
    ASSERT_EQ_INT(1, count_name(drv, "a"));
    db_free(db);
    return 0;
}
#endif

/* Preprocessor directives cannot appear inside TEST_MAIN's macro arguments. */
static void run_platform_specific_tests(void)
{
#if NCD_PLATFORM_WINDOWS
    RUN_TEST(subdir_rescan_with_different_case_replaces_subtree);
#endif
}

TEST_MAIN(
    RUN_TEST(add_dir_is_append_only);
    RUN_TEST(add_dir_300k_appends_fast);
    RUN_TEST(path_index_finds_every_entry);
    RUN_TEST(path_index_case_rules);
    RUN_TEST(path_index_rebuilt_after_subtree_removal);
    RUN_TEST(children_index_lists_children);
    RUN_TEST(subdir_rescan_twice_has_no_duplicates);
    RUN_TEST(subdir_rescan_picks_up_new_and_removed_dirs);
    run_platform_specific_tests();
)
