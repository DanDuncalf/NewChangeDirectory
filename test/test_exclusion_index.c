/* test_exclusion_index.c -- compiled exclusion index, unified semantics */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/scanner.h"
#include "../src/ncd.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if NCD_PLATFORM_WINDOWS
#include <direct.h>
#define TEI_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TEI_MKDIR(p) mkdir(p, 0755)
#endif

static const char *PATTERNS[] = {
    "$recycle.bin", "\\Windows", "C:\\Program Files", "node_modules", "build\\obj",
    "*\\.git", "*.tmp", "\\proc", "cache", "D:\\Data\\old", "temp?", "\\Users\\x\\AppData",
    "dist", "logs\\archive", "*\\target\\*", "\\opt\\big"
};
static const char *PATHS[] = {
    "$Recycle.Bin", "$RECYCLE.BIN\\S-1", "Windows", "Windows\\System32", "WindowsApps",
    "Program Files", "Program Files\\x", "src\\node_modules", "node_modules", "a\\build\\obj",
    "build\\obj", "repo\\.git", ".git", "x.tmp", "dir\\x.tmp", "proc", "proc\\1", "cache",
    "a\\cache", "Data\\old", "temp1", "temp12", "Users\\x\\AppData\\Local", "dist", "logs\\archive",
    "p\\logs\\archive", "w\\target\\debug", "target\\debug", "opt\\big", "opt\\bigger", ""
};

/* Build metadata with the first n patterns. */
static NcdMetadata *meta_with(int n)
{
    NcdMetadata *m = db_metadata_create();
    for (int i = 0; i < n; i++) db_exclusion_add(m, PATTERNS[i]);
    return m;
}

static int differential(int n)
{
    NcdMetadata *m = meta_with(n);
    NcdExclusionIndex *idx = db_exclusion_index_build(&m->exclusions);
    ASSERT_TRUE(db_exclusion_index_is_hashed(idx) == (n > NCD_EXCLUSION_HASH_THRESHOLD));
    const char drives[] = { 'C', 'D', 0 };
    for (size_t d = 0; d < sizeof(drives); d++) {
        for (size_t i = 0; i < sizeof(PATHS) / sizeof(PATHS[0]); i++) {
            /* Oracle: the per-entry linear rules, evaluated pattern by pattern. */
            bool expect = false;
            for (int k = 0; k < m->exclusions.count && !expect; k++) {
                NcdExclusionList one = { &m->exclusions.entries[k], 1, 1 };
                NcdExclusionIndex *single = db_exclusion_index_build(&one);
                expect = db_exclusion_index_check(single, drives[d], PATHS[i]);
                db_exclusion_index_free(single);
            }
            bool got = db_exclusion_index_check(idx, drives[d], PATHS[i]);
            if (got != expect) {
                printf("  n=%d drive=%c path='%s' expect=%d got=%d\n", n, drives[d] ? drives[d] : '0', PATHS[i], expect, got);
                ASSERT_TRUE(0);
            }
        }
    }
    db_exclusion_index_free(idx);
    db_metadata_free(m);
    return 0;
}

TEST(index_linear_matches_rules_small_list) { return differential(6); }
TEST(index_hashed_matches_linear_large_list) { return differential(16); }
TEST(index_threshold_is_eight) {
    NcdMetadata *m8 = meta_with(8), *m9 = meta_with(9);
    NcdExclusionIndex *a = db_exclusion_index_build(&m8->exclusions);
    NcdExclusionIndex *b = db_exclusion_index_build(&m9->exclusions);
    ASSERT_FALSE(db_exclusion_index_is_hashed(a));
    ASSERT_TRUE(db_exclusion_index_is_hashed(b));
    db_exclusion_index_free(a); db_exclusion_index_free(b);
    db_metadata_free(m8); db_metadata_free(m9);
    return 0;
}

TEST(index_known_answers) {
    NcdMetadata *m = meta_with(16);
    NcdExclusionIndex *idx = db_exclusion_index_build(&m->exclusions);
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "$Recycle.Bin"));
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "Windows\\System32"));
    ASSERT_FALSE(db_exclusion_index_check(idx, 'C', "WindowsApps"));
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "Program Files\\x"));
    ASSERT_FALSE(db_exclusion_index_check(idx, 'D', "Program Files\\x"));   /* C: only */
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "a\\build\\obj"));        /* parent\child suffix */
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "repo\\.git"));           /* wildcard */
#endif
    ASSERT_FALSE(db_exclusion_index_check(idx, 'C', "unrelated\\dir"));
    db_exclusion_index_free(idx);
    db_metadata_free(m);
    return 0;
}

TEST(rel_path_strips_drive_and_separator) {
#if NCD_PLATFORM_WINDOWS
    ASSERT_EQ_STR("Windows\\System32", db_exclusion_rel_path("C:\\Windows\\System32"));
#endif
    ASSERT_EQ_STR("home/x", db_exclusion_rel_path("/home/x"));
    ASSERT_EQ_STR("rel\\dir", db_exclusion_rel_path("rel\\dir"));
    return 0;
}

TEST(full_path_fast_on_large_drive) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    char name[32];
    for (int i = 0; i < 200000; i++) {
        snprintf(name, sizeof(name), "n%07d", i);
        db_add_dir(drv, name, i < 50 ? -1 : (i / 50) - 1, false, false);
    }
    char p[NCD_MAX_PATH];
    clock_t t0 = clock();
    for (int i = 0; i < drv->dir_count; i++) db_full_path(drv, i, p, sizeof(p));
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  200K db_full_path calls: %.3f s\n", secs);
    ASSERT_TRUE(secs < 1.0);   /* old per-call heap bitset + memset: several seconds */
    db_free(db);
    return 0;
}

TEST(full_path_still_detects_cycles_and_bad_parents) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "a", -1, false, false);
    db_add_dir(drv, "b", 0, false, false);
    drv->dirs[0].parent = 1;                 /* cycle a <-> b */
    char p[NCD_MAX_PATH];
    ASSERT_NULL(db_full_path(drv, 1, p, sizeof(p)));
    drv->dirs[0].parent = 99;                /* out of range */
    ASSERT_NULL(db_full_path(drv, 1, p, sizeof(p)));
    db_free(db);
    return 0;
}

TEST(filter_excluded_same_result_large_list_and_no_copy_when_clean) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int win = db_add_dir(drv, "Windows", -1, false, false);
    db_add_dir(drv, "System32", win, false, false);
    int src = db_add_dir(drv, "src", -1, false, false);
    int nm = db_add_dir(drv, "node_modules", src, false, false);
    db_add_dir(drv, "lodash", nm, false, false);
    db_add_dir(drv, "keep", src, false, false);
    NcdMetadata *m = meta_with(16);
    int removed = db_filter_excluded(db, m);
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(removed >= 2);               /* Windows + System32 */
#else
    (void)removed;                           /* Linux rules differ on drive-qualified patterns */
#endif
    char p[NCD_MAX_PATH];
    bool keep_found = false;
    for (int i = 0; i < drv->dir_count; i++) {
        db_full_path(drv, i, p, sizeof(p));
        if (strstr(p, "keep")) keep_found = true;
        ASSERT_TRUE(strstr(p, "System32") == NULL);
    }
    ASSERT_TRUE(keep_found);

    /* clean db: nothing excluded -> blob stays a blob (no mutable copy) */
    NcdDatabase *clean = db_create();
    DriveData *cd = db_add_drive(clean, 'C');
    db_add_dir(cd, "nothing_here", -1, false, false);
    clean->is_blob = false;
    ASSERT_EQ_INT(0, db_filter_excluded(clean, m));
    db_metadata_free(m);
    db_free(db);
    db_free(clean);
    return 0;
}

/* Scanner now uses the same rules: a literal non-root pattern skips the dir. */
TEST(scanner_skips_literal_excluded_dir) {
    char cwd[NCD_MAX_PATH], root[NCD_MAX_PATH], p[NCD_MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));
    snprintf(root, sizeof(root), "%s%stei_tree", cwd, NCD_PATH_SEP);
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", root);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
#endif
    system(cmd);
    TEI_MKDIR(root);
    snprintf(p, sizeof(p), "%s%skeepme", root, NCD_PATH_SEP); TEI_MKDIR(p);
    snprintf(p, sizeof(p), "%s%sskipme_lit", root, NCD_PATH_SEP); TEI_MKDIR(p);
    snprintf(p, sizeof(p), "%s%sskipme_lit%sinner", root, NCD_PATH_SEP, NCD_PATH_SEP); TEI_MKDIR(p);

    NcdMetadata *m = db_metadata_create();
    /* Rooted literal relative to the drive root, built from the real path. */
    char pat[NCD_MAX_PATH];
    snprintf(pat, sizeof(pat), "%s%s%sskipme_lit", NCD_PATH_SEP, db_exclusion_rel_path(root), NCD_PATH_SEP);
    ASSERT_TRUE(db_exclusion_add(m, pat));
    for (int i = 0; i < 10; i++) {             /* push it over the hash threshold */
        char filler[32]; snprintf(filler, sizeof(filler), "filler_%d", i);
        db_exclusion_add(m, filler);
    }
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(root);
    scan_subdirectory(db, letter, root, true, true, NULL, NULL, &m->exclusions);
    DriveData *drv = db_find_drive(db, letter);
    ASSERT_NOT_NULL(drv);
    bool keep = false, skip = false, inner = false;
    for (int i = 0; i < drv->dir_count; i++) {
        const char *n = drv->name_pool + drv->dirs[i].name_off;
        if (!strcmp(n, "keepme")) keep = true;
        if (!strcmp(n, "skipme_lit")) skip = true;
        if (!strcmp(n, "inner")) inner = true;
    }
    ASSERT_TRUE(keep);
    ASSERT_FALSE(skip);
    ASSERT_FALSE(inner);
    db_free(db);
    db_metadata_free(m);
    return 0;
}

TEST_MAIN(
    RUN_TEST(index_linear_matches_rules_small_list);
    RUN_TEST(index_hashed_matches_linear_large_list);
    RUN_TEST(index_threshold_is_eight);
    RUN_TEST(index_known_answers);
    RUN_TEST(rel_path_strips_drive_and_separator);
    RUN_TEST(full_path_fast_on_large_drive);
    RUN_TEST(full_path_still_detects_cycles_and_bad_parents);
    RUN_TEST(filter_excluded_same_result_large_list_and_no_copy_when_clean);
    RUN_TEST(scanner_skips_literal_excluded_dir);
)
