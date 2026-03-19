/* test_matcher.c -- Tests for matcher module */
#include "test_framework.h"
#include "../src/matcher.h"
#include "../src/database.h"
#include <string.h>

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

void suite_matcher(void) {
    RUN_TEST(match_single_component);
    RUN_TEST(match_two_components);
    RUN_TEST(match_case_insensitive);
    RUN_TEST(match_with_hidden_filter);
    RUN_TEST(match_no_results);
    RUN_TEST(match_empty_search);
    RUN_TEST(match_prefix_single_component);
    RUN_TEST(match_prefix_multi_component);
}

TEST_MAIN(
    RUN_SUITE(matcher);
)
