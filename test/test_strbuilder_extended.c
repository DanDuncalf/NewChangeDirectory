/* test_strbuilder_extended.c -- Extended StringBuilder tests for better coverage */
#include "test_framework.h"
#include "../../shared/strbuilder.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================ sb_init Tests */

TEST(init_with_null_buffer) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* After init, buf should be allocated */
    ASSERT_NOT_NULL(sb.buf);
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_TRUE(sb.cap > 0);
    
    sb_free(&sb);
    return 0;
}

TEST(init_multiple_independent) {
    StringBuilder sb1, sb2;
    sb_init(&sb1);
    sb_init(&sb2);
    
    sb_append(&sb1, "first");
    sb_append(&sb2, "second");
    
    ASSERT_EQ_STR("first", sb1.buf);
    ASSERT_EQ_STR("second", sb2.buf);
    
    sb_free(&sb1);
    sb_free(&sb2);
    return 0;
}

/* ================================================================ sb_append Tests */

TEST(append_empty_string) {
    StringBuilder sb;
    sb_init(&sb);
    
    bool result = sb_append(&sb, "");
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_EQ_STR("", sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(append_null_pointer) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Appending NULL should fail gracefully */
    bool result = sb_append(&sb, NULL);
    
    /* Behavior depends on implementation - just verify no crash */
    (void)result;
    
    sb_free(&sb);
    return 0;
}

TEST(append_exactly_fills_capacity) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Find initial capacity */
    size_t cap = sb.cap;
    
    /* Append exactly cap-1 characters (leaving room for null terminator) */
    char *str = (char *)malloc(cap);
    memset(str, 'A', cap - 1);
    str[cap - 1] = '\0';
    
    bool result = sb_append(&sb, str);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT((int)(cap - 1), (int)sb.len);
    ASSERT_EQ_INT((int)cap, (int)sb.cap);  /* Should not have grown */
    
    free(str);
    sb_free(&sb);
    return 0;
}

TEST(append_triggers_realloc) {
    StringBuilder sb;
    sb_init(&sb);
    
    size_t initial_cap = sb.cap;
    
    /* Append more than capacity to trigger realloc */
    char *str = (char *)malloc(initial_cap * 3);
    memset(str, 'B', initial_cap * 3 - 1);
    str[initial_cap * 3 - 1] = '\0';
    
    bool result = sb_append(&sb, str);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(sb.cap > initial_cap);
    ASSERT_EQ_INT((int)(initial_cap * 3 - 1), (int)sb.len);
    
    free(str);
    sb_free(&sb);
    return 0;
}

TEST(append_unicode_characters) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Append UTF-8 characters */
    bool result = sb_append(&sb, "Hello \xc3\xa9\xe2\x9c\x93");  /* Hello é✓ */
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(sb.len > 6);
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_appendn Tests */

TEST(appendn_zero_bytes) {
    StringBuilder sb;
    sb_init(&sb);
    
    bool result = sb_appendn(&sb, "hello", 0);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(0, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(appendn_exceeds_source_length) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Request more bytes than string has */
    bool result = sb_appendn(&sb, "hi", 100);
    
    /* Should only append what's available */
    ASSERT_TRUE(result);
    ASSERT_TRUE(sb.len >= 2);
    
    sb_free(&sb);
    return 0;
}

TEST(appendn_exact_length) {
    StringBuilder sb;
    sb_init(&sb);
    
    bool result = sb_appendn(&sb, "hello", 5);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(5, (int)sb.len);
    ASSERT_TRUE(memcmp(sb.buf, "hello", 5) == 0);
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_appendc Tests */

TEST(appendc_null_character) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Append null character */
    sb_appendc(&sb, '\0');
    
    /* Length should increase by 1 */
    ASSERT_EQ_INT(1, (int)sb.len);
    ASSERT_EQ_INT(0, sb.buf[0]);
    
    sb_free(&sb);
    return 0;
}

TEST(appendc_special_characters) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_appendc(&sb, '\n');
    sb_appendc(&sb, '\r');
    sb_appendc(&sb, '\t');
    
    ASSERT_EQ_INT(3, (int)sb.len);
    ASSERT_EQ_INT('\n', sb.buf[0]);
    ASSERT_EQ_INT('\r', sb.buf[1]);
    ASSERT_EQ_INT('\t', sb.buf[2]);
    
    sb_free(&sb);
    return 0;
}

TEST(appendc_many_characters) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Append many characters to trigger multiple reallocations */
    for (int i = 0; i < 1000; i++) {
        sb_appendc(&sb, 'A' + (i % 26));
    }
    
    ASSERT_EQ_INT(1000, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_appendf Tests */

TEST(appendf_empty_format) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_appendf(&sb, "");
    
    ASSERT_EQ_INT(0, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(appendf_no_format_args) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_appendf(&sb, "Hello World");
    
    ASSERT_EQ_STR("Hello World", sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(appendf_multiple_format_args) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_appendf(&sb, "%s: %d + %d = %d", "Math", 2, 3, 5);
    
    ASSERT_EQ_STR("Math: 2 + 3 = 5", sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(appendf_large_output) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Generate large output that exceeds initial capacity */
    sb_appendf(&sb, "%01000d", 0);
    
    ASSERT_EQ_INT(1000, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(appendf_hex_and_octal) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_appendf(&sb, "0x%X 0%o", 255, 8);
    
    ASSERT_EQ_STR("0xFF 010", sb.buf);
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_append_json_str Tests */

TEST(append_json_str_null) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append_json_str(&sb, NULL);
    
    /* Should output "null" or empty string */
    ASSERT_TRUE(sb.len > 0);
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_empty) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append_json_str(&sb, "");
    
    ASSERT_EQ_STR("\"\"", sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_quotes) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append_json_str(&sb, "say \"hello\"");
    
    ASSERT_STR_CONTAINS(sb.buf, "\\\"");
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_backslash) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append_json_str(&sb, "C:\\Windows\\System32");
    
    ASSERT_STR_CONTAINS(sb.buf, "\\\\");
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_all_control_chars) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Test all control characters that need escaping */
    sb_append_json_str(&sb, "\x08\x0c\n\r\t");  /* \b\f\n\r\t */
    
    ASSERT_STR_CONTAINS(sb.buf, "\\b");
    ASSERT_STR_CONTAINS(sb.buf, "\\f");
    ASSERT_STR_CONTAINS(sb.buf, "\\n");
    ASSERT_STR_CONTAINS(sb.buf, "\\r");
    ASSERT_STR_CONTAINS(sb.buf, "\\t");
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_unicode) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* UTF-8 characters */
    sb_append_json_str(&sb, "Caf\xc3\xa9");  /* Café */
    
    ASSERT_TRUE(sb.len > 0);
    
    sb_free(&sb);
    return 0;
}

TEST(append_json_str_high_control_chars) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Characters 0x00-0x1F should be escaped as \u00XX */
    char str[2] = {0x01, 0};
    sb_append_json_str(&sb, str);
    
    ASSERT_STR_CONTAINS(sb.buf, "\\u00");
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_clear Tests */

TEST(clear_on_empty_builder) {
    StringBuilder sb;
    sb_init(&sb);
    
    size_t old_cap = sb.cap;
    sb_clear(&sb);
    
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_EQ_INT((int)old_cap, (int)sb.cap);
    ASSERT_NOT_NULL(sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(clear_then_append) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_append(&sb, "hello");
    sb_clear(&sb);
    sb_append(&sb, "world");
    
    ASSERT_EQ_STR("world", sb.buf);
    
    sb_free(&sb);
    return 0;
}

TEST(clear_preserves_capacity) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Make it grow */
    for (int i = 0; i < 1000; i++) {
        sb_append(&sb, "x");
    }
    
    size_t grown_cap = sb.cap;
    sb_clear(&sb);
    
    /* Capacity should be preserved */
    ASSERT_EQ_INT((int)grown_cap, (int)sb.cap);
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_steal Tests */

TEST(steal_returns_allocated_memory) {
    StringBuilder sb;
    sb_init(&sb);
    sb_append(&sb, "test string");
    
    char *stolen = sb_steal(&sb);
    
    ASSERT_NOT_NULL(stolen);
    ASSERT_EQ_STR("test string", stolen);
    
    /* StringBuilder should be reset */
    ASSERT_NULL(sb.buf);
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_EQ_INT(0, (int)sb.cap);
    
    free(stolen);
    return 0;
}

TEST(steal_from_empty_builder) {
    StringBuilder sb;
    sb_init(&sb);
    
    char *stolen = sb_steal(&sb);
    
    /* Should return empty string or NULL */
    if (stolen != NULL) {
        ASSERT_EQ_STR("", stolen);
        free(stolen);
    }
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_dup Tests */

TEST(dup_returns_copy) {
    StringBuilder sb;
    sb_init(&sb);
    sb_append(&sb, "original");
    
    char *copy = sb_dup(&sb);
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_STR("original", copy);
    
    /* Modifying copy should not affect original */
    copy[0] = 'X';
    ASSERT_EQ_STR("original", sb.buf);
    
    free(copy);
    sb_free(&sb);
    return 0;
}

TEST(dup_empty_builder) {
    StringBuilder sb;
    sb_init(&sb);
    
    char *copy = sb_dup(&sb);
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_STR("", copy);
    
    free(copy);
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_ensure_cap Tests */

TEST(ensure_cap_same_capacity) {
    StringBuilder sb;
    sb_init(&sb);
    
    size_t current_cap = sb.cap;
    bool result = sb_ensure_cap(&sb, current_cap);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT((int)current_cap, (int)sb.cap);
    
    sb_free(&sb);
    return 0;
}

TEST(ensure_cap_less_than_current) {
    StringBuilder sb;
    sb_init(&sb);
    
    size_t current_cap = sb.cap;
    bool result = sb_ensure_cap(&sb, current_cap / 2);
    
    /* Should succeed but not shrink */
    ASSERT_TRUE(result);
    ASSERT_EQ_INT((int)current_cap, (int)sb.cap);
    
    sb_free(&sb);
    return 0;
}

TEST(ensure_cap_huge_request) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Request very large capacity */
    bool result = sb_ensure_cap(&sb, 1024 * 1024);  /* 1MB */
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(sb.cap >= 1024 * 1024);
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ sb_free Tests */

TEST(free_null_pointer) {
    /* Should not crash */
    sb_free(NULL);
    
    return 0;
}

TEST(free_empty_builder) {
    StringBuilder sb;
    sb_init(&sb);
    
    sb_free(&sb);
    
    /* After free, buf should be NULL */
    ASSERT_NULL(sb.buf);
    ASSERT_EQ_INT(0, (int)sb.len);
    ASSERT_EQ_INT(0, (int)sb.cap);
    
    return 0;
}

TEST(double_free_safe) {
    StringBuilder sb;
    sb_init(&sb);
    sb_append(&sb, "test");
    
    sb_free(&sb);
    sb_free(&sb);  /* Double free should be safe (no-op) */
    
    return 0;
}

/* ================================================================ Complex Usage Tests */

TEST(append_then_clear_multiple_times) {
    StringBuilder sb;
    sb_init(&sb);
    
    for (int i = 0; i < 10; i++) {
        sb_appendf(&sb, "iteration %d", i);
        ASSERT_TRUE(sb.len > 0);
        sb_clear(&sb);
    }
    
    ASSERT_EQ_INT(0, (int)sb.len);
    
    sb_free(&sb);
    return 0;
}

TEST(json_object_building) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Build a simple JSON object */
    sb_append(&sb, "{");
    sb_append(&sb, "\"name\":");
    sb_append_json_str(&sb, "John Doe");
    sb_append(&sb, ",");
    sb_append(&sb, "\"path\":");
    sb_append_json_str(&sb, "C:\\Users\\John");
    sb_append(&sb, "}");
    
    ASSERT_STR_CONTAINS(sb.buf, "\"name\"");
    ASSERT_STR_CONTAINS(sb.buf, "\"John Doe\"");
    ASSERT_STR_CONTAINS(sb.buf, "\\\\");  /* Escaped backslash */
    
    sb_free(&sb);
    return 0;
}

TEST(rapid_append_and_clear) {
    StringBuilder sb;
    sb_init(&sb);
    
    /* Rapid fire operations */
    for (int i = 0; i < 100; i++) {
        sb_append(&sb, "x");
        if (i % 10 == 0) {
            sb_clear(&sb);
        }
    }
    
    ASSERT_TRUE(sb.len >= 0);
    
    sb_free(&sb);
    return 0;
}

/* ================================================================ Test Suite */

void suite_strbuilder_extended(void) {
    /* sb_init tests */
    RUN_TEST(init_with_null_buffer);
    RUN_TEST(init_multiple_independent);
    
    /* sb_append tests */
    RUN_TEST(append_empty_string);
    RUN_TEST(append_null_pointer);
    RUN_TEST(append_exactly_fills_capacity);
    RUN_TEST(append_triggers_realloc);
    RUN_TEST(append_unicode_characters);
    
    /* sb_appendn tests */
    RUN_TEST(appendn_zero_bytes);
    RUN_TEST(appendn_exceeds_source_length);
    RUN_TEST(appendn_exact_length);
    
    /* sb_appendc tests */
    RUN_TEST(appendc_null_character);
    RUN_TEST(appendc_special_characters);
    RUN_TEST(appendc_many_characters);
    
    /* sb_appendf tests */
    RUN_TEST(appendf_empty_format);
    RUN_TEST(appendf_no_format_args);
    RUN_TEST(appendf_multiple_format_args);
    RUN_TEST(appendf_large_output);
    RUN_TEST(appendf_hex_and_octal);
    
    /* sb_append_json_str tests */
    RUN_TEST(append_json_str_null);
    RUN_TEST(append_json_str_empty);
    RUN_TEST(append_json_str_quotes);
    RUN_TEST(append_json_str_backslash);
    RUN_TEST(append_json_str_all_control_chars);
    RUN_TEST(append_json_str_unicode);
    RUN_TEST(append_json_str_high_control_chars);
    
    /* sb_clear tests */
    RUN_TEST(clear_on_empty_builder);
    RUN_TEST(clear_then_append);
    RUN_TEST(clear_preserves_capacity);
    
    /* sb_steal tests */
    RUN_TEST(steal_returns_allocated_memory);
    RUN_TEST(steal_from_empty_builder);
    
    /* sb_dup tests */
    RUN_TEST(dup_returns_copy);
    RUN_TEST(dup_empty_builder);
    
    /* sb_ensure_cap tests */
    RUN_TEST(ensure_cap_same_capacity);
    RUN_TEST(ensure_cap_less_than_current);
    RUN_TEST(ensure_cap_huge_request);
    
    /* sb_free tests */
    RUN_TEST(free_null_pointer);
    RUN_TEST(free_empty_builder);
    RUN_TEST(double_free_safe);
    
    /* Complex usage tests */
    RUN_TEST(append_then_clear_multiple_times);
    RUN_TEST(json_object_building);
    RUN_TEST(rapid_append_and_clear);
}

TEST_MAIN(
    RUN_SUITE(strbuilder_extended);
)
