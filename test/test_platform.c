/* test_platform.c -- Tests for platform abstraction module */
#include "test_framework.h"
#include "../src/platform.h"
#include <string.h>
#include <ctype.h>

TEST(strcasestr_finds_substring) {
    const char *haystack = "Hello World";
    const char *result = platform_strcasestr(haystack, "World");
    
    ASSERT_NOT_NULL(result);
    ASSERT_EQ_STR("World", result);
    
    return 0;
}

TEST(strcasestr_case_insensitive) {
    const char *haystack = "Hello World";
    const char *result1 = platform_strcasestr(haystack, "WORLD");
    ASSERT_NOT_NULL(result1);
    
    const char *result2 = platform_strcasestr(haystack, "world");
    ASSERT_NOT_NULL(result2);
    
    const char *result3 = platform_strcasestr(haystack, "WoRlD");
    ASSERT_NOT_NULL(result3);
    
    return 0;
}

TEST(strcasestr_returns_null_on_miss) {
    const char *haystack = "Hello World";
    const char *result = platform_strcasestr(haystack, "xyz");
    
    ASSERT_NULL(result);
    
    return 0;
}

TEST(is_drive_specifier_valid) {
    /* Valid drive specifiers */
    ASSERT_TRUE(platform_is_drive_specifier("C:"));
    ASSERT_TRUE(platform_is_drive_specifier("c:"));
    ASSERT_TRUE(platform_is_drive_specifier("Z:"));
    
    return 0;
}

TEST(is_drive_specifier_invalid) {
    /* Invalid drive specifiers */
    ASSERT_FALSE(platform_is_drive_specifier("C"));      /* Missing colon */
    ASSERT_FALSE(platform_is_drive_specifier(":"));      /* Missing letter */
    ASSERT_FALSE(platform_is_drive_specifier("1:"));     /* Number not letter */
    ASSERT_FALSE(platform_is_drive_specifier("CC:"));    /* Two letters */
    ASSERT_FALSE(platform_is_drive_specifier(""));       /* Empty */
    ASSERT_FALSE(platform_is_drive_specifier("C:\\path")); /* Has path */
    
    return 0;
}

TEST(parse_drive_from_search_extracts_letter) {
    char clean[256];
    
    /* Parse "C:downloads" */
    char drive = platform_parse_drive_from_search("C:downloads", clean, sizeof(clean));
    
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(drive == 'C' || drive == 'c');
    ASSERT_EQ_STR("downloads", clean);
#else
    /* On Linux, drive letters are mapped differently */
    (void)drive;
    ASSERT_TRUE(strlen(clean) > 0);
#endif
    
    return 0;
}

TEST(parse_drive_from_search_no_drive) {
    char clean[256];
    
    /* Parse search without drive */
    char drive = platform_parse_drive_from_search("downloads", clean, sizeof(clean));
    
    ASSERT_EQ_INT(0, drive);  /* No drive specified */
    ASSERT_EQ_STR("downloads", clean);
    
    return 0;
}

TEST(build_mount_path_valid_letter) {
    char path[MAX_PATH];
    bool result = platform_build_mount_path('C', path, sizeof(path));
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(path) > 0);
    
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(path[0] == 'C' || path[0] == 'c');
    ASSERT_TRUE(path[1] == ':');
    ASSERT_TRUE(path[2] == '\\');
#endif
    
    return 0;
}

TEST(filter_available_drives_filters_correctly) {
    /* Mock input drives */
    char in_drives[] = {'C', 'D', 'E', 'F'};
    int in_count = 4;
    
    /* Create skip mask: skip D and F */
    bool skip_mask[26] = {false};
    skip_mask['D' - 'A'] = true;
    skip_mask['F' - 'A'] = true;
    
    char out_drives[26];
    int out_count = platform_filter_available_drives(in_drives, in_count, 
                                                      skip_mask, out_drives, 26);
    
    ASSERT_EQ_INT(2, out_count);  /* Only C and E remain */
    ASSERT_TRUE(out_drives[0] == 'C' || out_drives[0] == 'c');
    ASSERT_TRUE(out_drives[1] == 'E' || out_drives[1] == 'e');
    
    return 0;
}

TEST(is_pseudo_fs_matches_known_types) {
    /* Known pseudo filesystems */
    ASSERT_TRUE(platform_is_pseudo_fs("proc"));
    ASSERT_TRUE(platform_is_pseudo_fs("sysfs"));
    ASSERT_TRUE(platform_is_pseudo_fs("tmpfs"));
    ASSERT_TRUE(platform_is_pseudo_fs("devtmpfs"));
    ASSERT_TRUE(platform_is_pseudo_fs("cgroup"));
    ASSERT_TRUE(platform_is_pseudo_fs("devpts"));
    
    /* Non-pseudo filesystems */
    ASSERT_FALSE(platform_is_pseudo_fs("ext4"));
    ASSERT_FALSE(platform_is_pseudo_fs("ntfs"));
    ASSERT_FALSE(platform_is_pseudo_fs("xfs"));
    ASSERT_FALSE(platform_is_pseudo_fs("btrfs"));
    
    return 0;
}

TEST(get_app_title_returns_non_null) {
    const char *title = platform_get_app_title();
    
    ASSERT_NOT_NULL(title);
    ASSERT_TRUE(strlen(title) > 0);
    
    return 0;
}

TEST(db_default_path_returns_valid_path) {
    char path[MAX_PATH];
    bool result = ncd_platform_db_default_path(path, sizeof(path));
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(path) > 0);
    
    /* Path should contain database-related substring */
    ASSERT_TRUE(strstr(path, "ncd") != NULL || strstr(path, "NCD") != NULL);
    
    return 0;
}

/* Test suites */
void suite_platform(void) {
    RUN_TEST(strcasestr_finds_substring);
    RUN_TEST(strcasestr_case_insensitive);
    RUN_TEST(strcasestr_returns_null_on_miss);
    RUN_TEST(is_drive_specifier_valid);
    RUN_TEST(is_drive_specifier_invalid);
    RUN_TEST(parse_drive_from_search_extracts_letter);
    RUN_TEST(parse_drive_from_search_no_drive);
    RUN_TEST(build_mount_path_valid_letter);
    RUN_TEST(filter_available_drives_filters_correctly);
    RUN_TEST(is_pseudo_fs_matches_known_types);
    RUN_TEST(get_app_title_returns_non_null);
    RUN_TEST(db_default_path_returns_valid_path);
}

/* Main */
TEST_MAIN(
    RUN_SUITE(platform);
)
