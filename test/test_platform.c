/* test_platform.c -- Tests for platform abstraction module */
#include "test_framework.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
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
    /* Valid drive specifiers - Windows only */
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(platform_is_drive_specifier("C:"));
    ASSERT_TRUE(platform_is_drive_specifier("c:"));
    ASSERT_TRUE(platform_is_drive_specifier("Z:"));
#else
    /* On Linux, drive specifiers are not used */
    ASSERT_FALSE(platform_is_drive_specifier("C:"));
#endif
    
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
    
#if NCD_PLATFORM_WINDOWS
    /* On Windows, should default to current drive */
    char cwd[MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));
    char expected_drive = (char)toupper((unsigned char)cwd[0]);
    ASSERT_EQ_INT(expected_drive, drive);
#else
    /* On Linux/WSL: returns either:
     * - Drive letter from CWD (e.g., 'C' if in /mnt/c/...)
     * - First available mount's drive letter
     * - 0x01 if no mounts available
     */
    bool is_valid_drive = (drive >= 'A' && drive <= 'Z') || drive == '\x01';
    ASSERT_TRUE(is_valid_drive);
#endif
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

TEST(system_mode_paths_do_not_depend_on_elevation) {
    char default_path[MAX_PATH];
    char drive_path[MAX_PATH];
    bool previous_mode = ncd_is_system_mode();

    /* A normal client must use the same shared namespace as the elevated
     * service.  Privilege affects write access, not namespace selection. */
    ncd_set_system_mode(true);
    bool default_ok = ncd_platform_db_default_path(default_path, sizeof(default_path));
    bool drive_ok = ncd_platform_db_drive_path('C', drive_path, sizeof(drive_path));
    bool default_is_system = default_ok && strstr(default_path, NCD_SYSTEM_DIR) != NULL;
    bool drive_is_system = drive_ok && strstr(drive_path, NCD_SYSTEM_DIR) != NULL;

    ncd_set_system_mode(previous_mode);
    ASSERT_TRUE(default_ok);
    ASSERT_TRUE(drive_ok);
    ASSERT_TRUE(default_is_system);
    ASSERT_TRUE(drive_is_system);
    return 0;
}

/* ================================================================ Usable Volume Tests */
/* Unformatted (RAW) volumes, empty/CD drives and unused letters must never be
 * scanned. A RAW volume cannot be created here without Windows offering to
 * format it, so these cover the classifier on real and missing volumes and
 * check that every enumerated drive passes it. */

TEST(volume_usable_rejects_null_and_empty) {
    ASSERT_FALSE(platform_volume_is_usable(NULL));
    ASSERT_FALSE(platform_volume_is_usable(""));
    return 0;
}

#if NCD_PLATFORM_WINDOWS
TEST(volume_usable_accepts_system_drive) {
    const char *sd = getenv("SystemDrive");
    char root[4] = { (sd && sd[0]) ? sd[0] : 'C', ':', '\\', '\0' };
    ASSERT_TRUE(platform_volume_is_usable(root));
    return 0;
}

TEST(volume_usable_rejects_unused_letter) {
    unsigned long mask = platform_get_logical_drives_mask();
    int found = 0;
    for (int i = 25; i >= 2; i--) {           /* Z..C */
        if (mask & (1ul << i)) continue;
        char root[4] = { (char)('A' + i), ':', '\\', '\0' };
        ASSERT_FALSE(platform_volume_is_usable(root));
        found = 1;
        break;
    }
    if (!found) SKIP_TEST("every drive letter is in use");
    return 0;
}

TEST(volume_usable_rejects_non_root_path_and_garbage) {
    ASSERT_FALSE(platform_volume_is_usable("not-a-drive"));
    return 0;
}
#else
TEST(volume_usable_accepts_root_dir) {
    ASSERT_TRUE(platform_volume_is_usable("/"));
    return 0;
}

TEST(volume_usable_rejects_missing_dir) {
    ASSERT_FALSE(platform_volume_is_usable("/nonexistent_ncd_volume_xyz"));
    return 0;
}
#endif

TEST(enumerated_drives_are_all_usable) {
    char drives[32];
    int n = platform_get_available_drives(drives, 27);
    for (int i = 0; i < n; i++) {
        char mount[MAX_PATH];
        if (!platform_build_mount_path(drives[i], mount, sizeof(mount))) continue;
#if NCD_PLATFORM_WINDOWS
        ASSERT_TRUE(platform_volume_is_usable(mount));
#else
        (void)mount;  /* Linux mounts come from /proc/mounts */
#endif
    }

    char mount_bufs[26][MAX_PATH];
    const char *mount_ptrs[26];
    int m = ncd_platform_enumerate_mounts(mount_bufs, mount_ptrs, MAX_PATH, 26);
    for (int i = 0; i < m; i++) {
        ASSERT_TRUE(platform_volume_is_usable(mount_ptrs[i]));
    }
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
    RUN_TEST(system_mode_paths_do_not_depend_on_elevation);
    /* Usable volume tests (skip unformatted / empty / missing drives) */
    RUN_TEST(volume_usable_rejects_null_and_empty);
#if NCD_PLATFORM_WINDOWS
    RUN_TEST(volume_usable_accepts_system_drive);
    RUN_TEST(volume_usable_rejects_unused_letter);
    RUN_TEST(volume_usable_rejects_non_root_path_and_garbage);
#else
    RUN_TEST(volume_usable_accepts_root_dir);
    RUN_TEST(volume_usable_rejects_missing_dir);
#endif
    RUN_TEST(enumerated_drives_are_all_usable);
}

/* Main */
TEST_MAIN(
    RUN_SUITE(platform);
)
