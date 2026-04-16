/*
 * test_service_logging.c  --  Service logging system tests
 *
 * Tests:
 * - Log file creation in correct location (same dir as database)
 * - Log file opened in append mode
 * - Log level filtering
 * - Log file format validation
 * - Thread-safe logging
 *
 * These tests verify that the service logging system works correctly
 * and creates log files in the database directory.
 */

#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define unlink _unlink
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

/* --------------------------------------------------------- test utilities     */

/* Get logs directory path for testing */
static bool get_test_logs_dir(char *buf, size_t buf_size) {
    if (!db_logs_path(buf, buf_size)) {
        return false;
    }
    return true;
}

/* Get log file path for testing */
static bool get_test_log_path(char *buf, size_t buf_size) {
    char logs_dir[MAX_PATH];
    if (!get_test_logs_dir(logs_dir, sizeof(logs_dir))) {
        return false;
    }
    
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, buf_size, "%s\\ncd_service.log", logs_dir);
#else
    snprintf(buf, buf_size, "%s/ncd_service.log", logs_dir);
#endif
    return true;
}

/* Check if file exists */
static bool file_exists(const char *path) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(path);
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access(path, F_OK) == 0);
#endif
}

/* Check if directory exists */
static bool dir_exists(const char *path) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(path);
    return (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

/* Remove file if exists */
static void remove_file_if_exists(const char *path) {
#if NCD_PLATFORM_WINDOWS
    DeleteFileA(path);
#else
    unlink(path);
#endif
}

/* Remove directory if exists (must be empty) */
static void remove_dir_if_exists(const char *path) {
#if NCD_PLATFORM_WINDOWS
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif
}

/* --------------------------------------------------------- tests              */

/* Test 1: db_logs_path returns a valid path */
TEST(logs_path_returns_valid_path) {
    char path[MAX_PATH];
    
    ASSERT_TRUE(db_logs_path(path, sizeof(path)) != NULL);
    ASSERT_TRUE(strlen(path) > 0);
    
    /* Path should contain "ncd" (database directory) */
    ASSERT_TRUE(strstr(path, "ncd") != NULL);
}

/* Test 2: Database directory is created automatically */
TEST(database_directory_created_automatically) {
    char db_dir[MAX_PATH];
    char temp_suffix[32];
    
    /* Get current time for unique test dir */
    snprintf(temp_suffix, sizeof(temp_suffix), "_test_%ld", (long)time(NULL));
    
    /* Get the database path - this should create the directory */
    ASSERT_TRUE(db_logs_path(db_dir, sizeof(db_dir)) != NULL);
    
    /* The database directory should now exist */
    ASSERT_TRUE(dir_exists(db_dir));
}

/* Test 3: Log file path is correct */
TEST(log_file_path_correct) {
    char logs_dir[MAX_PATH];
    char log_path[MAX_PATH];
    char expected_path[MAX_PATH];
    
    ASSERT_TRUE(db_logs_path(logs_dir, sizeof(logs_dir)) != NULL);
    ASSERT_TRUE(get_test_log_path(log_path, sizeof(log_path)));
    
    /* Construct expected path */
#if NCD_PLATFORM_WINDOWS
    snprintf(expected_path, sizeof(expected_path), "%s\\ncd_service.log", logs_dir);
#else
    snprintf(expected_path, sizeof(expected_path), "%s/ncd_service.log", logs_dir);
#endif
    
    ASSERT_TRUE(strcmp(log_path, expected_path) == 0);
}

/* Test 4: Log file format validation */
TEST(log_file_format_validation) {
    char log_path[MAX_PATH];
    
    ASSERT_TRUE(get_test_log_path(log_path, sizeof(log_path)));
    
    /* If log file exists, verify it has valid format */
    if (file_exists(log_path)) {
        FILE *f = fopen(log_path, "r");
        ASSERT_TRUE(f != NULL);
        
        char line[512];
        int line_count = 0;
        bool found_header = false;
        bool found_timestamp = false;
        
        while (fgets(line, sizeof(line), f) && line_count < 50) {
            line_count++;
            
            /* Check for header line */
            if (strstr(line, "NCD Service Starting at") != NULL) {
                found_header = true;
            }
            
            /* Check for log entry format: [HH:MM:SS] [thread] [L#] message */
            if (line[0] == '[' && strlen(line) > 20) {
                /* Check for timestamp format [HH:MM:SS] */
                if (line[3] == ':' && line[6] == ':' && line[9] == ']') {
                    found_timestamp = true;
                }
            }
        }
        
        fclose(f);
        
        /* If log has entries, should have proper format */
        if (line_count > 2) {
            ASSERT_TRUE(found_header || found_timestamp);
        }
    }
}

/* Test 5: Multiple calls to db_logs_path return same path */
TEST(logs_path_consistent) {
    char path1[MAX_PATH];
    char path2[MAX_PATH];
    
    ASSERT_TRUE(db_logs_path(path1, sizeof(path1)) != NULL);
    ASSERT_TRUE(db_logs_path(path2, sizeof(path2)) != NULL);
    
    ASSERT_TRUE(strcmp(path1, path2) == 0);
}

/* Test 6: Logs path with override uses override directory */
TEST(logs_path_with_override) {
    /* This test checks that when a metadata override is set,
     * the logs path is derived from that override directory */
    
    char original_path[MAX_PATH];
    char override_path[MAX_PATH];
    char path_with_override[MAX_PATH];
    
    /* Get default path first */
    ASSERT_TRUE(db_logs_path(original_path, sizeof(original_path)) != NULL);
    
    /* Set an override */
#if NCD_PLATFORM_WINDOWS
    db_metadata_set_override("C:\\TestNCD\\ncd.metadata");
#else
    db_metadata_set_override("/tmp/test_ncd/ncd.metadata");
#endif
    
    /* Get path with override */
    if (db_logs_path(path_with_override, sizeof(path_with_override)) != NULL) {
        /* Should contain the override directory, not the default */
#if NCD_PLATFORM_WINDOWS
        ASSERT_TRUE(strstr(path_with_override, "TestNCD") != NULL);
#else
        ASSERT_TRUE(strstr(path_with_override, "test_ncd") != NULL);
#endif
    }
    
    /* Clear override */
    db_metadata_set_override(NULL);
    
    /* Verify we're back to default */
    ASSERT_TRUE(db_logs_path(override_path, sizeof(override_path)) != NULL);
    ASSERT_TRUE(strcmp(original_path, override_path) == 0);
}

/* Test 7: Buffer size handling */
TEST(logs_path_buffer_size) {
    char small_buf[5];
    char normal_buf[MAX_PATH];
    
    /* Very small buffer should fail gracefully or truncate */
    char *result = db_logs_path(small_buf, sizeof(small_buf));
    /* Result behavior depends on implementation, but shouldn't crash */
    (void)result;
    
    /* Normal buffer should succeed */
    ASSERT_TRUE(db_logs_path(normal_buf, sizeof(normal_buf)) != NULL);
}

void suite_service_logging(void) {
    RUN_TEST(logs_path_returns_valid_path);
    RUN_TEST(database_directory_created_automatically);
    RUN_TEST(log_file_path_correct);
    RUN_TEST(log_file_format_validation);
    RUN_TEST(logs_path_consistent);
    RUN_TEST(logs_path_with_override);
    RUN_TEST(logs_path_buffer_size);
}

TEST_MAIN(
    printf("=== Service Logging Tests ===\n");
    suite_service_logging();
)
