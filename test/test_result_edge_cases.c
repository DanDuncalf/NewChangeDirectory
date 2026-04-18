/* test_result_edge_cases.c -- Result output edge case tests for NCD (Agent 4) */
#include "test_framework.h"
#include "../src/result.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Helper to read result file content */
static char *read_result_file(void) {
    char path[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    const char *temp = getenv("TEMP");
    if (!temp) temp = ".";
    snprintf(path, sizeof(path), "%s\\%s", temp, NCD_RESULT_FILE);
#else
    snprintf(path, sizeof(path), "/tmp/%s", NCD_RESULT_FILE);
#endif
    
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = (char *)malloc(size + 1);
    if (content) {
        fread(content, 1, size, f);
        content[size] = '\0';
    }
    fclose(f);
    
    return content;
}

/* Helper to clean up result file */
static void cleanup_result_file(void) {
    char path[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    const char *temp = getenv("TEMP");
    if (!temp) temp = ".";
    snprintf(path, sizeof(path), "%s\\%s", temp, NCD_RESULT_FILE);
#else
    snprintf(path, sizeof(path), "/tmp/%s", NCD_RESULT_FILE);
#endif
    remove(path);
}

/* ================================================================ Batch File - Windows (10 tests) */

TEST(result_batch_file_percent_escaping) {
    cleanup_result_file();
    
    /* Path with percent signs needs escaping in batch files */
    result_ok("C:\\Users\\%USERNAME%\\Documents", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    
    /* Percent signs should be escaped as %% in batch files */
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_bang_escaping) {
    cleanup_result_file();
    
    /* Path with exclamation marks needs escaping in delayed expansion */
    result_ok("C:\\Path\\!test!\\dir", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_caret_escaping) {
    cleanup_result_file();
    
    /* Path with carets */
    result_ok("C:\\Test^Path", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_ampersand_escaping) {
    cleanup_result_file();
    
    /* Path with ampersands needs escaping */
    result_ok("C:\\A&B\\Test", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_pipe_escaping) {
    cleanup_result_file();
    
    /* Path with pipe character */
    result_ok("C:\\A|B\\Test", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_quote_escaping) {
    cleanup_result_file();
    
    /* Path with quotes */
    result_ok("C:\\\"Program Files\"\\Test", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_redirection_escaping) {
    cleanup_result_file();
    
    /* Path with redirection characters */
    result_ok("C:\\Test>File", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_parenthesis_escaping) {
    cleanup_result_file();
    
    /* Path with parentheses */
    result_ok("C:\\Test(1)\\Dir", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_newline_replacement) {
    cleanup_result_file();
    
    /* Path with embedded newlines should be sanitized */
    result_ok("C:\\Test\nLine", 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_batch_file_maximum_length) {
    cleanup_result_file();
    
    /* Very long path */
    char long_path[NCD_MAX_PATH];
    strcpy(long_path, "C:\\");
    for (int i = 0; i < 200 && strlen(long_path) < NCD_MAX_PATH - 20; i++) {
        strcat(long_path, "verylongdirectoryname");
        if (i < 199) strcat(long_path, "\\");
    }
    
    result_ok(long_path, 'C');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

/* ================================================================ Shell Script - Linux (5 tests) */

TEST(result_shell_file_dollar_escaping) {
    cleanup_result_file();
    
    /* Path with dollar signs needs escaping in shell */
    result_ok("/home/$USER/test", '\0');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_shell_file_backtick_escaping) {
    cleanup_result_file();
    
    /* Path with backticks */
    result_ok("/home/`whoami`/test", '\0');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_shell_file_backslash_escaping) {
    cleanup_result_file();
    
    /* Path with backslashes */
    result_ok("/home/test\\dir", '\0');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_shell_file_quote_escaping) {
    cleanup_result_file();
    
    /* Path with quotes */
    result_ok("/home/\"quoted\"/dir", '\0');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_shell_file_newline_replacement) {
    cleanup_result_file();
    
    /* Path with newlines */
    result_ok("/home/test\nline", '\0');
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_PATH");
    
    free(content);
    cleanup_result_file();
    return 0;
}

/* ================================================================ Error Messages (5 tests) */

TEST(result_error_message_with_path) {
    cleanup_result_file();
    
    result_error("Path not found: %s", "C:\\NonExistent");
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_STATUS");
    ASSERT_STR_CONTAINS(content, "ERROR");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_error_message_with_suggestion) {
    cleanup_result_file();
    
    result_error("Did you mean: %s?", "downloads");
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_MESSAGE");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_error_message_with_help_text) {
    cleanup_result_file();
    
    result_error("Use /? for help. Error: %s", "invalid option");
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_MESSAGE");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_error_message_truncation) {
    cleanup_result_file();
    
    /* Very long error message */
    char long_msg[4096];
    memset(long_msg, 'A', 4095);
    long_msg[4095] = '\0';
    
    result_error("%s", long_msg);
    
    char *content = read_result_file();
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "NCD_STATUS");
    
    free(content);
    cleanup_result_file();
    return 0;
}

TEST(result_error_message_format_consistency) {
    cleanup_result_file();
    
    /* Multiple error calls should produce consistent format */
    result_error("Error 1");
    
    char *content1 = read_result_file();
    ASSERT_NOT_NULL(content1);
    
    free(content1);
    cleanup_result_file();
    
    result_error("Error 2: %s", "details");
    
    char *content2 = read_result_file();
    ASSERT_NOT_NULL(content2);
    
    /* Both should have NCD_STATUS */
    ASSERT_STR_CONTAINS(content2, "NCD_STATUS");
    
    free(content2);
    cleanup_result_file();
    return 0;
}

/* ================================================================ Test Suite */

void suite_result_edge_cases(void) {
    /* Batch File - Windows (10 tests) */
    RUN_TEST(result_batch_file_percent_escaping);
    RUN_TEST(result_batch_file_bang_escaping);
    RUN_TEST(result_batch_file_caret_escaping);
    RUN_TEST(result_batch_file_ampersand_escaping);
    RUN_TEST(result_batch_file_pipe_escaping);
    RUN_TEST(result_batch_file_quote_escaping);
    RUN_TEST(result_batch_file_redirection_escaping);
    RUN_TEST(result_batch_file_parenthesis_escaping);
    RUN_TEST(result_batch_file_newline_replacement);
    RUN_TEST(result_batch_file_maximum_length);
    
    /* Shell Script - Linux (5 tests) */
    RUN_TEST(result_shell_file_dollar_escaping);
    RUN_TEST(result_shell_file_backtick_escaping);
    RUN_TEST(result_shell_file_backslash_escaping);
    RUN_TEST(result_shell_file_quote_escaping);
    RUN_TEST(result_shell_file_newline_replacement);
    
    /* Error Messages (5 tests) */
    RUN_TEST(result_error_message_with_path);
    RUN_TEST(result_error_message_with_suggestion);
    RUN_TEST(result_error_message_with_help_text);
    RUN_TEST(result_error_message_truncation);
    RUN_TEST(result_error_message_format_consistency);
}

TEST_MAIN(
    RUN_SUITE(result_edge_cases);
)
