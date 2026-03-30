/* test_strbuilder.c -- Tests for StringBuilder module */
#include "test_framework.h"
#include "../../shared/strbuilder.h"
#include <string.h>

TEST(init_sets_valid_state) {
    StringBuilder sb;
    sb_init(&sb);
    
    ASSERT_NOT_NULL(sb.buf);
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_TRUE(sb.cap > 0);
    ASSERT_EQ_STR("", sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(append_single_string) {
    StringBuilder sb;
    sb_init(&sb);
    
    bool result = sb_append(&sb, "hello");
    ASSERT_TRUE(result);
    ASSERT_EQ_STR("hello", sb.buf);
    ASSERT_EQ_INT(5, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(append_multiple_strings_concatenates) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append(&sb, "hello");
    sb_append(&sb, " ");
    sb_append(&sb, "world");
    
    ASSERT_EQ_STR("hello world", sb.buf);
    ASSERT_EQ_INT(11, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(appendn_truncates_at_n_bytes) {
    StringBuilder sb;
    sb_init(&sb);
    
    bool result = sb_appendn(&sb, "helloworld", 5);
    ASSERT_TRUE(result);
    ASSERT_EQ_STR("hello", sb.buf);
    ASSERT_EQ_INT(5, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(appendc_single_character) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_appendc(&sb, 'H');
    sb_appendc(&sb, 'i');
    
    ASSERT_EQ_STR("Hi", sb.buf);
    ASSERT_EQ_INT(2, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(appendf_formatted_output) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_appendf(&sb, "Value: %d, String: %s", 42, "test");
    
    ASSERT_EQ_STR("Value: 42, String: test", sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_escapes_quotes) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append_json_str(&sb, "say \"hello\"");
    
    ASSERT_STR_CONTAINS(sb.buf, "\\\"");
    ASSERT_TRUE(strstr(sb.buf, "say") != NULL);
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_escapes_backslash) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append_json_str(&sb, "C:\\path\\to\\file");
    
    ASSERT_STR_CONTAINS(sb.buf, "\\\\");
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_escapes_control_chars) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append_json_str(&sb, "line1\nline2\ttab");
    
    ASSERT_STR_CONTAINS(sb.buf, "\\n");
    ASSERT_STR_CONTAINS(sb.buf, "\\t");
    
    sb_free(&sb);
    return 0;
}

TEST(clear_resets_length_but_keeps_buffer) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append(&sb, "hello world");
    size_t old_cap = sb.cap;
    
    sb_clear(&sb);
    
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_EQ_STR("", sb.buf);
    ASSERT_EQ_INT((int)old_cap, (int)sb.cap);
    
    sb_free(&sb);
    return 0;
}

TEST(steal_transfers_ownership) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append(&sb, "stolen");
    char *stolen = sb_steal(&sb);
    
    ASSERT_NOT_NULL(stolen);
    ASSERT_EQ_STR("stolen", stolen);
    ASSERT_NULL(sb.buf);
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_EQ_INT(0, (int)sb.cap);
    
    free(stolen);
    sb_free(&sb);
    return 0;
}

TEST(dup_copies_buffer) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append(&sb, "original");
    char *copy = sb_dup(&sb);
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_STR("original", copy);
    ASSERT_NOT_NULL(sb.buf);
    
    free(copy);
    sb_free(&sb);
    return 0;
}

TEST(ensure_cap_grows_buffer) {
    StringBuilder sb;
    sb_init(&sb);
    
    size_t initial_cap = sb.cap;
    bool result = sb_ensure_cap(&sb, initial_cap * 4);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(sb.cap >= initial_cap * 4);
    
    sb_free(&sb);
    return 0;
}

TEST(large_append_triggers_realloc) {
    StringBuilder sb;
    sb_init(&sb);
    
    size_t initial_cap = sb.cap;
    for (int i = 0; i < 1000; i++) {
        sb_append(&sb, "aaaaaaaaaa");
    }
    
    ASSERT_TRUE(sb.cap > initial_cap);
    ASSERT_EQ_INT(10000, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(free_then_init_cycle) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append(&sb, "first");
    sb_free(&sb);
    
    sb_init(&sb);
    sb_append(&sb, "second");
    
    ASSERT_EQ_STR("second", sb.buf);
    
    sb_free(&sb);
    return 0;
}

void suite_strbuilder(void) {
    RUN_TEST(init_sets_valid_state);
    RUN_TEST(append_single_string);
    RUN_TEST(append_multiple_strings_concatenates);
    RUN_TEST(appendn_truncates_at_n_bytes);
    RUN_TEST(appendc_single_character);
    RUN_TEST(appendf_formatted_output);
    RUN_TEST(append_json_str_escapes_quotes);
    RUN_TEST(append_json_str_escapes_backslash);
    RUN_TEST(append_json_str_escapes_control_chars);
    RUN_TEST(clear_resets_length_but_keeps_buffer);
    RUN_TEST(steal_transfers_ownership);
    RUN_TEST(dup_copies_buffer);
    RUN_TEST(ensure_cap_grows_buffer);
    RUN_TEST(large_append_triggers_realloc);
    RUN_TEST(free_then_init_cycle);
}

TEST_MAIN(
    RUN_SUITE(strbuilder);
)
