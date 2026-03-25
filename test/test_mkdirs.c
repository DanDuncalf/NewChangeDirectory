/* test_mkdirs.c -- Tests for mkdirs agent command */
#include "test_framework.h"
#include "../src/ncd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#else
#include <unistd.h>
#endif

/* Helper to create temp directory path */
static void get_temp_dir(char *buf, size_t size, const char *suffix) {
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    snprintf(buf, size, "%s/ncd_mkdirs_test_%s", tmp, suffix);
}

/* Helper to check if directory exists */
static bool dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && (st.st_mode & S_IFDIR));
}

/* Helper to recursively remove a directory */
static void rm_rf(const char *path) {
#if NCD_PLATFORM_WINDOWS
    /* Windows: Use system command for recursive delete */
    char cmd[NCD_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", path);
    system(cmd);
#else
    /* Linux: Use rm -rf */
    char cmd[NCD_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" 2>/dev/null", path);
    system(cmd);
#endif
}

TEST(mkdirs_flat_format_simple) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "flat_simple");
    rm_rf(test_dir);
    
    /* Create a flat format file */
    char flat_file[NCD_MAX_PATH];
    snprintf(flat_file, sizeof(flat_file), "%s/flat.txt", test_dir);
    
    /* Create parent directory for the file */
    mkdir(test_dir, 0755);
    
    FILE *f = fopen(flat_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "parent\n");
    fprintf(f, "  child1\n");
    fprintf(f, "  child2\n");
    fclose(f);
    
    /* Run mkdirs command */
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\"", flat_file);
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs --file \"%s\"", flat_file);
#endif
    int ret = system(cmd);
    
    /* Verify directories were created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/parent", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/parent/child1", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/parent/child2", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_flat_format_nested) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "flat_nested");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Create a flat format file with nested structure */
    char flat_file[NCD_MAX_PATH];
    snprintf(flat_file, sizeof(flat_file), "%s/flat.txt", test_dir);
    
    FILE *f = fopen(flat_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "projects\n");
    fprintf(f, "  ncd\n");
    fprintf(f, "    src\n");
    fprintf(f, "    test\n");
    fprintf(f, "  shared\n");
    fprintf(f, "    platform\n");
    fclose(f);
    
    /* Run mkdirs command */
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\"", flat_file);
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs --file \"%s\"", flat_file);
#endif
    system(cmd);
    
    /* Verify directories were created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/projects", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/projects/ncd/src", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/projects/ncd/test", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/projects/shared/platform", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_flat_format_empty_lines) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "flat_empty");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Create a flat format file with empty lines */
    char flat_file[NCD_MAX_PATH];
    snprintf(flat_file, sizeof(flat_file), "%s/flat.txt", test_dir);
    
    FILE *f = fopen(flat_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "\n");
    fprintf(f, "root\n");
    fprintf(f, "\n");
    fprintf(f, "  subdir\n");
    fprintf(f, "\n");
    fclose(f);
    
    /* Run mkdirs command */
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\"", flat_file);
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs --file \"%s\"", flat_file);
#endif
    system(cmd);
    
    /* Verify directories were created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/root", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/root/subdir", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_json_format_simple) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "json_simple");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Create a JSON format file */
    char json_file[NCD_MAX_PATH];
    snprintf(json_file, sizeof(json_file), "%s/tree.json", test_dir);
    
    FILE *f = fopen(json_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "[\n");
    fprintf(f, "  {\"name\":\"docs\",\"children\":[{\"name\":\"api\"}]},\n");
    fprintf(f, "  {\"name\":\"src\"}\n");
    fprintf(f, "]\n");
    fclose(f);
    
    /* Run mkdirs command */
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\"", json_file);
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs --file \"%s\"", json_file);
#endif
    system(cmd);
    
    /* Verify directories were created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/docs", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/docs/api", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/src", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_json_format_nested) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "json_nested");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Create a JSON format file with deeply nested structure */
    char json_file[NCD_MAX_PATH];
    snprintf(json_file, sizeof(json_file), "%s/tree.json", test_dir);
    
    FILE *f = fopen(json_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "[\n");
    fprintf(f, "  {\n");
    fprintf(f, "    \"name\":\"root\",\n");
    fprintf(f, "    \"children\":[\n");
    fprintf(f, "      {\n");
    fprintf(f, "        \"name\":\"level1\",\n");
    fprintf(f, "        \"children\":[\n");
    fprintf(f, "          {\"name\":\"level2\",\"children\":[{\"name\":\"level3\"}]}\n");
    fprintf(f, "        ]\n");
    fprintf(f, "      }\n");
    fprintf(f, "    ]\n");
    fprintf(f, "  }\n");
    fprintf(f, "]\n");
    fclose(f);
    
    /* Run mkdirs command */
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\"", json_file);
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs --file \"%s\"", json_file);
#endif
    system(cmd);
    
    /* Verify directories were created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/root", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/root/level1", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/root/level1/level2", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/root/level1/level2/level3", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_json_format_string_array) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "json_strings");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Create a JSON format file with simple string array */
    char json_file[NCD_MAX_PATH];
    snprintf(json_file, sizeof(json_file), "%s/tree.json", test_dir);
    
    FILE *f = fopen(json_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "[\"dir1\", \"dir2\", \"dir3\"]\n");
    fclose(f);
    
    /* Run mkdirs command */
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\"", json_file);
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs --file \"%s\"", json_file);
#endif
    system(cmd);
    
    /* Verify directories were created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/dir1", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/dir2", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    snprintf(path, sizeof(path), "%s/dir3", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_json_inline_argument) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "json_inline");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Run mkdirs command with inline JSON */
    char cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), 
        ".\\NewChangeDirectory.exe /agent mkdirs \"[{\\\"name\\\":\\\"inline_dir\\\"}]\"",
        test_dir);
#else
    snprintf(cmd, sizeof(cmd), 
        "./NewChangeDirectory /agent mkdirs '[{\"name\":\"inline_dir\"}]'",
        test_dir);
#endif
    
    /* Change to test directory first */
    char cd_cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    snprintf(cd_cmd, sizeof(cd_cmd), "cd /d \"%s\" && %s", test_dir, cmd);
#else
    snprintf(cd_cmd, sizeof(cd_cmd), "cd \"%s\" && %s", test_dir, cmd);
#endif
    system(cd_cmd);
    
    /* Verify directory was created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/inline_dir", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_json_output_format) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "json_output");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Create a JSON format file */
    char json_file[NCD_MAX_PATH];
    snprintf(json_file, sizeof(json_file), "%s/tree.json", test_dir);
    
    FILE *f = fopen(json_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "[{\"name\":\"output_test\"}]\n");
    fclose(f);
    
    /* Run mkdirs command with JSON output */
    char cmd[NCD_MAX_PATH * 2];
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/output.txt", test_dir);
    
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), 
        ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\" --json > \"%s\" 2>&1",
        json_file, output_file);
#else
    snprintf(cmd, sizeof(cmd), 
        "./NewChangeDirectory /agent mkdirs --file \"%s\" --json > \"%s\" 2>&1",
        json_file, output_file);
#endif
    system(cmd);
    
    /* Read and verify output */
    FILE *out = fopen(output_file, "r");
    ASSERT_NOT_NULL(out);
    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, out);
    fclose(out);
    
    /* Should contain JSON */
    ASSERT_STR_CONTAINS(buf, "\"v\":1");
    ASSERT_STR_CONTAINS(buf, "\"dirs\"");
    ASSERT_STR_CONTAINS(buf, "\"result\"");
    
    /* Verify directory was created */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/output_test", test_dir);
    ASSERT_TRUE(dir_exists(path));
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_missing_file_error) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "missing");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);
    
    /* Try to use a non-existent file */
    char missing_file[NCD_MAX_PATH];
    snprintf(missing_file, sizeof(missing_file), "%s/does_not_exist.txt", test_dir);
    
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs --file \"%s\"", missing_file);
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs --file \"%s\"", missing_file);
#endif
    int ret = system(cmd);
    
    /* Command should return non-zero (error) */
    ASSERT_TRUE(ret != 0);
    
    /* Cleanup */
    rm_rf(test_dir);
    return 0;
}

TEST(mkdirs_no_input_error) {
    /* Run mkdirs with no input - should error */
    char cmd[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), ".\\NewChangeDirectory.exe /agent mkdirs");
#else
    snprintf(cmd, sizeof(cmd), "./NewChangeDirectory /agent mkdirs");
#endif
    int ret = system(cmd);
    
    /* Command should return non-zero (error) */
    ASSERT_TRUE(ret != 0);
    
    return 0;
}

void suite_mkdirs(void) {
    RUN_TEST(mkdirs_flat_format_simple);
    RUN_TEST(mkdirs_flat_format_nested);
    RUN_TEST(mkdirs_flat_format_empty_lines);
    RUN_TEST(mkdirs_json_format_simple);
    RUN_TEST(mkdirs_json_format_nested);
    RUN_TEST(mkdirs_json_format_string_array);
    RUN_TEST(mkdirs_json_inline_argument);
    RUN_TEST(mkdirs_json_output_format);
    RUN_TEST(mkdirs_missing_file_error);
    RUN_TEST(mkdirs_no_input_error);
}

TEST_MAIN(
    RUN_SUITE(mkdirs);
)
