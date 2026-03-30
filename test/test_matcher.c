/* test_matcher.c -- Tests for matcher module */
#include "test_framework.h"
#include "../src/matcher.h"
#include "../src/database.h"
#include <string.h>
#include <ctype.h>

/* Helper to create test database with known structure */
static NcdDatabase *create_test_db(void) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Build tree: C:\Users\Scott\Downloads
     *             C:\Users\Admin\Downloads
     *             C:\Windows\System32\drivers */
    int users = db_add_dir(drv, "Users", -1, false, false);
    int windows = db_add_dir(drv, "Windows", -1, false, true);
    
    int scott = db_add_dir(drv, "Scott", users, false, false);
    int admin = db_add_dir(drv, "Admin", users, false, false);
    int system32 = db_add_dir(drv, "System32", windows, false, true);
    
    db_add_dir(drv, "Downloads", scott, false, false);
    db_add_dir(drv, "Downloads", admin, false, false);
    db_add_dir(drv, "drivers", system32, false, false);
    
    return db;
}

TEST(match_single_component) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    NcdMatch *matches = matcher_find(db, "drivers", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(1, count);
    ASSERT_STR_CONTAINS(matches[0].full_path, "drivers");
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_two_components) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* Should find both Users\Scott and Users\Admin paths to Downloads */
    NcdMatch *matches = matcher_find(db, "Users\\Downloads", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(2, count);
    ASSERT_STR_CONTAINS(matches[0].full_path, "Downloads");
    ASSERT_STR_CONTAINS(matches[1].full_path, "Downloads");
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_case_insensitive) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    NcdMatch *matches = matcher_find(db, "USERS\\downloads", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(2, count);
    ASSERT_STR_CONTAINS(matches[0].full_path, "Downloads");
    ASSERT_STR_CONTAINS(matches[1].full_path, "Downloads");
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_with_hidden_filter) {
    NcdDatabase *db = create_test_db();
    DriveData *drv = &db->drives[0];
    
    /* Add hidden directory */
    db_add_dir(drv, "HiddenDir", -1, true, false);
    
    int count = 0;
    /* Without hidden flag */
    NcdMatch *matches = matcher_find(db, "Hidden", false, false, &count);
    ASSERT_NULL(matches);
    
    /* With hidden flag */
    matches = matcher_find(db, "Hidden", true, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(1, count);
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_no_results) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    NcdMatch *matches = matcher_find(db, "NonExistent", false, false, &count);
    ASSERT_NULL(matches);
    ASSERT_EQ_INT(0, count);
    
    db_free(db);
    return 0;
}

TEST(match_empty_search) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    NcdMatch *matches = matcher_find(db, "", false, false, &count);
    ASSERT_NULL(matches);
    
    db_free(db);
    return 0;
}

TEST(match_prefix_single_component) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* Should match "drivers" with prefix "driv" */
    NcdMatch *matches = matcher_find(db, "driv", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(1, count);
    ASSERT_STR_CONTAINS(matches[0].full_path, "drivers");
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_prefix_multi_component) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* Should match "System32\drivers" with "Sys\driv" */
    NcdMatch *matches = matcher_find(db, "Sys\\driv", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(1, count);
    ASSERT_STR_CONTAINS(matches[0].full_path, "drivers");
    
    free(matches);
    db_free(db);
    return 0;
}

/* Glob pattern tests */
TEST(match_glob_star_suffix) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "driv*" should match "drivers" */
    NcdMatch *matches = matcher_find(db, "driv*", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(1, count);
    ASSERT_STR_CONTAINS(matches[0].full_path, "drivers");
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_glob_star_prefix) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "*loads" should match "Downloads" */
    NcdMatch *matches = matcher_find(db, "*loads", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(2, count);  /* Both Scott\Downloads and Admin\Downloads */
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_glob_star_both) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "*own*" should match "Downloads" */
    NcdMatch *matches = matcher_find(db, "*own*", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(2, count);  /* Both Downloads entries */
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_glob_question_single) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "Sys?em32" should match "System32" (need include_system=true since System32 is marked as system) */
    NcdMatch *matches = matcher_find(db, "Sys?em32", false, true, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(1, count);
    ASSERT_STR_CONTAINS(matches[0].full_path, "System32");
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_glob_question_multiple) {
    NcdDatabase *db = create_test_db();
    DriveData *drv = &db->drives[0];
    
    /* Add test directories */
    db_add_dir(drv, "src_x64", -1, false, false);
    db_add_dir(drv, "src-x64", -1, false, false);
    db_add_dir(drv, "srcAx64", -1, false, false);
    db_add_dir(drv, "src__x64", -1, false, false);  /* Should NOT match */
    
    int count = 0;
    /* "src?x64" should match directories with exactly one char between */
    NcdMatch *matches = matcher_find(db, "src?x64", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(3, count);  /* src_x64, src-x64, srcAx64 */
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_glob_in_path) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "Us*\Down*" should match Users\Scott\Downloads and Users\Admin\Downloads */
    NcdMatch *matches = matcher_find(db, "Us*\\Down*", false, false, &count);
    ASSERT_NOT_NULL(matches);
    ASSERT_EQ_INT(2, count);
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(match_glob_no_match) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "xyz*" should not match anything */
    NcdMatch *matches = matcher_find(db, "xyz*", false, false, &count);
    ASSERT_NULL(matches);
    ASSERT_EQ_INT(0, count);
    
    db_free(db);
    return 0;
}

/* Fuzzy matching tests */
TEST(fuzzy_match_with_typo) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "downlaods" (transposed letters) should match "Downloads" */
    NcdMatch *matches = matcher_find_fuzzy(db, "downlaods", false, false, &count);
    
    /* Should find at least one match with fuzzy matching */
    ASSERT_NOT_NULL(matches);
    ASSERT_TRUE(count >= 1);
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(fuzzy_match_with_transposition) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "Dirvers" instead of "drivers" */
    NcdMatch *matches = matcher_find_fuzzy(db, "Dirvers", false, false, &count);
    
    /* Should find "drivers" despite the typo */
    ASSERT_NOT_NULL(matches);
    ASSERT_TRUE(count >= 1);
    ASSERT_STR_CONTAINS(matches[0].full_path, "drivers");
    
    free(matches);
    db_free(db);
    return 0;
}

TEST(fuzzy_match_no_results_on_total_mismatch) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* Complete gibberish - should not match anything even with fuzzy */
    NcdMatch *matches = matcher_find_fuzzy(db, "xyzqwerty12345", false, false, &count);
    
    /* No matches expected for total mismatch */
    ASSERT_NULL(matches);
    ASSERT_EQ_INT(0, count);
    
    db_free(db);
    return 0;
}

TEST(fuzzy_match_case_difference) {
    NcdDatabase *db = create_test_db();
    int count = 0;
    
    /* "DOWNLOADS" in all caps */
    NcdMatch *matches = matcher_find_fuzzy(db, "DOWNLOADS", false, false, &count);
    
    /* Should find "Downloads" entries */
    ASSERT_NOT_NULL(matches);
    ASSERT_TRUE(count >= 1);
    ASSERT_STR_CONTAINS(matches[0].full_path, "Downloads");
    
    free(matches);
    db_free(db);
    return 0;
}

/* Name index tests */
TEST(name_index_build_returns_non_null) {
    NcdDatabase *db = create_test_db();
    
    NameIndex *idx = name_index_build(db);
    ASSERT_NOT_NULL(idx);
    ASSERT_NOT_NULL(idx->entries);
    ASSERT_TRUE(idx->count > 0);
    
    name_index_free(idx);
    db_free(db);
    return 0;
}

TEST(name_index_find_by_hash_finds_entry) {
    NcdDatabase *db = create_test_db();
    NameIndex *idx = name_index_build(db);
    ASSERT_NOT_NULL(idx);
    
    /* Hash of "drivers" - we need to compute it the same way the matcher does */
    /* FNV-1a hash of lowercase "drivers" */
    const char *name = "drivers";
    uint32_t hash = 2166136261U;
    for (const char *p = name; *p; p++) {
        hash ^= (uint32_t)tolower((unsigned char)*p);
        hash *= 16777619U;
    }
    
    NameIndexEntry entries[10];
    int found = name_index_find_by_hash(idx, hash, entries, 10);
    
    ASSERT_TRUE(found >= 1);  /* At least one "drivers" entry found */
    
    name_index_free(idx);
    db_free(db);
    return 0;
}

TEST(name_index_find_by_hash_misses_unknown_hash) {
    NcdDatabase *db = create_test_db();
    NameIndex *idx = name_index_build(db);
    ASSERT_NOT_NULL(idx);
    
    /* Random hash that shouldn't match anything */
    uint32_t hash = 0xDEADBEEF;
    
    NameIndexEntry entries[10];
    int found = name_index_find_by_hash(idx, hash, entries, 10);
    
    ASSERT_EQ_INT(0, found);  /* No entries found */
    
    name_index_free(idx);
    db_free(db);
    return 0;
}

TEST(name_index_free_doesnt_crash) {
    NcdDatabase *db = create_test_db();
    NameIndex *idx = name_index_build(db);
    ASSERT_NOT_NULL(idx);
    
    /* Should not crash or leak */
    name_index_free(idx);
    
    /* Build another index to ensure cleanup was complete */
    idx = name_index_build(db);
    ASSERT_NOT_NULL(idx);
    name_index_free(idx);
    
    db_free(db);
    return 0;
}

void suite_matcher(void) {
    RUN_TEST(match_single_component);
    RUN_TEST(match_two_components);
    RUN_TEST(match_case_insensitive);
    RUN_TEST(match_with_hidden_filter);
    RUN_TEST(match_no_results);
    RUN_TEST(match_empty_search);
    RUN_TEST(match_prefix_single_component);
    RUN_TEST(match_prefix_multi_component);
    RUN_TEST(match_glob_star_suffix);
    RUN_TEST(match_glob_star_prefix);
    RUN_TEST(match_glob_star_both);
    RUN_TEST(match_glob_question_single);
    RUN_TEST(match_glob_question_multiple);
    RUN_TEST(match_glob_in_path);
    RUN_TEST(match_glob_no_match);
    /* Tier 1 fuzzy and name index tests */
    RUN_TEST(fuzzy_match_with_typo);
    RUN_TEST(fuzzy_match_with_transposition);
    RUN_TEST(fuzzy_match_no_results_on_total_mismatch);
    RUN_TEST(fuzzy_match_case_difference);
    RUN_TEST(name_index_build_returns_non_null);
    RUN_TEST(name_index_find_by_hash_finds_entry);
    RUN_TEST(name_index_find_by_hash_misses_unknown_hash);
    RUN_TEST(name_index_free_doesnt_crash);
}

TEST_MAIN(
    RUN_SUITE(matcher);
)
