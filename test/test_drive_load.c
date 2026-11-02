/* test_drive_load.c -- Functional tests for bulk drive adoption and the loaders */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "../src/service_state.h"
#include "../src/state_backend.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if NCD_PLATFORM_WINDOWS
#include <direct.h>
#define TDL_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TDL_MKDIR(p) mkdir(p, 0755)
#endif

static int raw_add(DriveData *drv, const char *name, int parent)
{
    size_t nl = strlen(name) + 1;
    if (drv->name_pool_len + nl > drv->name_pool_cap) {
        drv->name_pool_cap = (drv->name_pool_cap ? drv->name_pool_cap * 2 : 4096) + nl;
        drv->name_pool = realloc(drv->name_pool, drv->name_pool_cap);
    }
    memcpy(drv->name_pool + drv->name_pool_len, name, nl);
    if (drv->dir_count >= drv->dir_capacity) {
        drv->dir_capacity = drv->dir_capacity ? drv->dir_capacity * 2 : 64;
        drv->dirs = realloc(drv->dirs, sizeof(DirEntry) * (size_t)drv->dir_capacity);
    }
    DirEntry *e = &drv->dirs[drv->dir_count];
    memset(e, 0, sizeof(*e));
    e->parent = parent;
    e->name_off = (uint32_t)drv->name_pool_len;
    drv->name_pool_len += nl;
    return drv->dir_count++;
}

static char g_base[NCD_MAX_PATH];
static char g_data[NCD_MAX_PATH];

static void env_setup(const char *suffix)
{
    const char *t = getenv("TEMP"); if (!t) t = getenv("TMP"); if (!t) t = "/tmp";
    snprintf(g_base, sizeof(g_base), "%s%sncd_tdl_%s", t, NCD_PATH_SEP, suffix);
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", g_base); system(cmd);
    TDL_MKDIR(g_base);
    snprintf(g_data, sizeof(g_data), "%s\\NCD", g_base); TDL_MKDIR(g_data);
    _putenv_s("LOCALAPPDATA", g_base);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_base); system(cmd);
    TDL_MKDIR(g_base);
    snprintf(g_data, sizeof(g_data), "%s/ncd", g_base); TDL_MKDIR(g_data);
    setenv("XDG_DATA_HOME", g_base, 1);
#endif
    ncd_set_system_mode(false);
}

static char test_letter(void)
{
    char drives[32];
    int n = platform_get_available_drives(drives, 27);
    return n > 0 ? drives[0] : 'C';
}

/* Tree with case-variant siblings: the exact shape that the old merge corrupted. */
static NcdDatabase *make_case_variant_db(char letter)
{
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    int root  = raw_add(drv, "root", -1);      /* 0 */
    int foo   = raw_add(drv, "Foo", root);     /* 1 */
    int foo2  = raw_add(drv, "foo", root);     /* 2 */
    raw_add(drv, "upper_child_x1", foo);       /* 3 */
    int lc = raw_add(drv, "lower_child_y2", foo2); /* 4 */
    raw_add(drv, "deep_leaf_z3", lc);          /* 5 */
    return db;
}

TEST(adopt_drive_copies_entries_verbatim) {
    NcdDatabase *src = make_case_variant_db('C');
    NcdDatabase *dst = db_create();
    DriveData *got = db_adopt_drive(dst, &src->drives[0]);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ_INT(6, got->dir_count);
    ASSERT_TRUE(memcmp(got->dirs, src->drives[0].dirs, 6 * sizeof(DirEntry)) == 0);
    ASSERT_TRUE(got->name_pool_len == src->drives[0].name_pool_len);
    ASSERT_TRUE(memcmp(got->name_pool, src->drives[0].name_pool, got->name_pool_len) == 0);
    ASSERT_TRUE(got->dirs != src->drives[0].dirs);          /* deep copy */
    char a[NCD_MAX_PATH], b[NCD_MAX_PATH];
    for (int i = 0; i < 6; i++) {
        db_full_path(&src->drives[0], i, a, sizeof(a));
        db_full_path(got, i, b, sizeof(b));
        ASSERT_EQ_STR(a, b);
    }
    db_free(src);
    db_free(dst);
    return 0;
}

TEST(adopt_drive_preserves_case_variant_parents) {
    NcdDatabase *src = make_case_variant_db('C');
    NcdDatabase *dst = db_create();
    DriveData *got = db_adopt_drive(dst, &src->drives[0]);
    char p[NCD_MAX_PATH];
    db_full_path(got, 5, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "foo");
    ASSERT_STR_CONTAINS(p, "lower_child_y2");
    ASSERT_STR_CONTAINS(p, "deep_leaf_z3");
    db_full_path(got, 3, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "Foo");
    db_free(src);
    db_free(dst);
    return 0;
}

TEST(adopt_drive_rejects_duplicate_letter) {
    NcdDatabase *src = make_case_variant_db('D');
    NcdDatabase *dst = db_create();
    ASSERT_NOT_NULL(db_adopt_drive(dst, &src->drives[0]));
    ASSERT_NULL(db_adopt_drive(dst, &src->drives[0]));
    ASSERT_EQ_INT(1, dst->drive_count);
    ASSERT_EQ_INT(6, dst->drives[0].dir_count);
    db_free(src);
    db_free(dst);
    return 0;
}

TEST(adopt_drive_empty_drive) {
    NcdDatabase *src = db_create();
    db_add_drive(src, 'E');
    NcdDatabase *dst = db_create();
    DriveData *got = db_adopt_drive(dst, &src->drives[0]);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ_INT(0, got->dir_count);
    ASSERT_NULL(got->dirs);
    db_free(src);
    db_free(dst);
    return 0;
}

static NcdDatabase *make_big_db(char letter, int n)
{
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    char name[32];
    for (int i = 0; i < n; i++) {
        snprintf(name, sizeof(name), "d%07d", i);
        raw_add(drv, name, i < 64 ? -1 : (i / 64) - 1);
    }
    return db;
}

TEST(load_all_drives_into_reads_files_and_is_fast) {
    env_setup("big");
    char letter = test_letter();
    NcdDatabase *big = make_big_db(letter, 250000);
    char path[NCD_MAX_PATH];
    ASSERT_NOT_NULL(db_drive_path(letter, path, sizeof(path)));
    ASSERT_TRUE(db_save_binary_single(big, 0, path));
    db_free(big);

    NcdDatabase *dst = db_create();
    clock_t t0 = clock();
    int loaded = db_load_all_drives_into(dst, NULL, NULL);
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  loaded %d file(s), 250000 dirs in %.3f s\n", loaded, secs);
    ASSERT_EQ_INT(1, loaded);
    DriveData *drv = db_find_drive(dst, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(250000, drv->dir_count);
    ASSERT_TRUE(secs < 1.0);   /* old O(n^2) merge: ~20 s for this size */
    db_free(dst);
    return 0;
}

static int g_progress_calls = 0;
static void count_progress(char l, int i, int t, void *ud) { (void)l; (void)i; (void)t; (void)ud; g_progress_calls++; }

TEST(load_all_drives_into_reports_progress) {
    env_setup("prog");
    g_progress_calls = 0;
    NcdDatabase *dst = db_create();
    db_load_all_drives_into(dst, count_progress, NULL);
    char drives[32];
    ASSERT_EQ_INT(platform_get_available_drives(drives, 27), g_progress_calls);
    db_free(dst);
    return 0;
}

TEST(service_loader_preserves_case_variant_parents) {
    env_setup("svc");
    char letter = test_letter();
    NcdDatabase *src = make_case_variant_db(letter);
    char path[NCD_MAX_PATH];
    ASSERT_NOT_NULL(db_drive_path(letter, path, sizeof(path)));
    ASSERT_TRUE(db_save_binary_single(src, 0, path));
    db_free(src);

    ServiceState *st = service_state_init();
    ASSERT_NOT_NULL(st);
    ASSERT_TRUE(service_state_load_databases(st));
    const NcdDatabase *db = service_state_get_database(st);
    DriveData *drv = db_find_drive((NcdDatabase *)db, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(6, drv->dir_count);
    char p[NCD_MAX_PATH];
    db_full_path(drv, 5, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "lower_child_y2");
    service_state_cleanup(st);
    return 0;
}

TEST(local_backend_loader_preserves_case_variant_parents) {
    env_setup("local");
    char letter = test_letter();
    NcdDatabase *src = make_case_variant_db(letter);
    char path[NCD_MAX_PATH];
    ASSERT_NOT_NULL(db_drive_path(letter, path, sizeof(path)));
    ASSERT_TRUE(db_save_binary_single(src, 0, path));
    db_free(src);

    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    ASSERT_EQ_INT(0, state_backend_open_local(&view, &info));
    const NcdDatabase *db = state_view_database(view);
    DriveData *drv = db_find_drive((NcdDatabase *)db, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(6, drv->dir_count);
    char p[NCD_MAX_PATH];
    db_full_path(drv, 5, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "deep_leaf_z3");
    ASSERT_STR_CONTAINS(p, "lower_child_y2");
    state_backend_close(view);
    return 0;
}

TEST_MAIN(
    RUN_TEST(adopt_drive_copies_entries_verbatim);
    RUN_TEST(adopt_drive_preserves_case_variant_parents);
    RUN_TEST(adopt_drive_rejects_duplicate_letter);
    RUN_TEST(adopt_drive_empty_drive);
    RUN_TEST(load_all_drives_into_reads_files_and_is_fast);
    RUN_TEST(load_all_drives_into_reports_progress);
    RUN_TEST(service_loader_preserves_case_variant_parents);
    RUN_TEST(local_backend_loader_preserves_case_variant_parents);
)
