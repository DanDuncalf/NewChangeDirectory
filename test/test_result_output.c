/* test_result_output.c -- Tests for result output functions */
#include "test_framework.h"
#include "../src/result.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ================================================================ Tier 3: Result Output Tests */

TEST(write_result_ok_doesnt_crash) {
    /* Since write_result writes to a fixed temp path, just verify it doesn't crash */
    write_result(true, "C:", "C:\\Users\\test", "Changed to C:\\Users\\test");
    
    return 0;
}

TEST(write_result_error_doesnt_crash) {
    write_result(false, "", "", "Test error message");
    
    return 0;
}

TEST(write_result_cancel_doesnt_crash) {
    write_result(false, "", "", "NCD: Cancelled.");
    
    return 0;
}

TEST(result_ok_doesnt_crash) {
    result_ok("C:\\Users\\test", 'C');
    
    return 0;
}

TEST(result_error_doesnt_crash) {
    result_error("Test error: %s", "something failed");
    
    return 0;
}

TEST(result_cancel_doesnt_crash) {
    result_cancel();
    
    return 0;
}

TEST(heur_sanitize_lowercases) {
    char src[] = "DOWNLOADS";
    char dst[256];
    heur_sanitize(src, dst, sizeof(dst), true);
    ASSERT_EQ_STR("downloads", dst);
    
    char src2[] = "MiXeD CaSe";
    heur_sanitize(src2, dst, sizeof(dst), true);
    ASSERT_EQ_STR("mixed case", dst);
    
    return 0;
}

TEST(heur_sanitize_no_lower_preserved) {
    char src[] = "DOWNLOADS";
    char dst[256];
    heur_sanitize(src, dst, sizeof(dst), false);
    ASSERT_EQ_STR("DOWNLOADS", dst);
    
    return 0;
}

TEST(heur_sanitize_trims_whitespace) {
    char src[] = "  downloads  ";
    char dst[256];
    heur_sanitize(src, dst, sizeof(dst), true);
    ASSERT_EQ_STR("downloads", dst);
    
    return 0;
}

TEST(heur_sanitize_handles_empty) {
    char src[] = "";
    char dst[256];
    heur_sanitize(src, dst, sizeof(dst), true);
    ASSERT_EQ_STR("", dst);
    
    return 0;
}

TEST(heur_promote_match_moves_to_top) {
    /* Create test matches */
    NcdMatch matches[3];
    strcpy(matches[0].full_path, "C:\\Users\\admin\\Downloads");
    matches[0].drive_letter = 'C';
    strcpy(matches[1].full_path, "C:\\Users\\scott\\Downloads");
    matches[1].drive_letter = 'C';
    strcpy(matches[2].full_path, "C:\\Windows\\Download");
    matches[2].drive_letter = 'C';
    
    /* Promote the second match to top */
    heur_promote_match(matches, 3, "C:\\Users\\scott\\Downloads");
    
    /* First match should now be the promoted one */
    ASSERT_STR_CONTAINS(matches[0].full_path, "scott");
    
    return 0;
}

TEST(heur_promote_match_no_match_found) {
    NcdMatch matches[2];
    strcpy(matches[0].full_path, "C:\\Users\\test");
    strcpy(matches[1].full_path, "C:\\Windows");
    
    /* Try to promote a path that doesn't exist */
    heur_promote_match(matches, 2, "C:\\NonExistent");
    
    /* Order should remain unchanged */
    ASSERT_STR_CONTAINS(matches[0].full_path, "Users");
    
    return 0;
}

TEST(heur_promote_match_null_or_empty) {
    NcdMatch matches[2];
    strcpy(matches[0].full_path, "C:\\test");
    strcpy(matches[1].full_path, "C:\\Windows");
    
    /* Should not crash with NULL */
    heur_promote_match(NULL, 2, "C:\\test");
    heur_promote_match(matches, 2, NULL);
    heur_promote_match(matches, 2, "");
    heur_promote_match(matches, 0, "C:\\test");
    heur_promote_match(matches, 1, "C:\\test"); /* count <= 1 should return early */
    
    return 0;
}

TEST(heur_sort_by_frequency_two_element_swap) {
    /* Minimal reproducer for the cycle-reorder corruption: sorting [A,B]
     * into [B,A] is a single 2-cycle permutation. A buggy in-place reorder
     * overwrites one slot twice, producing [B,B] and losing A. */
    NcdMatch matches[2];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\A");
    matches[0].frequency = 0;
    strcpy(matches[1].full_path, "C:\\B");
    matches[1].frequency = 5;

    heur_sort_by_frequency(matches, 2);

    ASSERT_EQ_STR("C:\\B", matches[0].full_path);
    ASSERT_EQ_STR("C:\\A", matches[1].full_path);

    return 0;
}

TEST(heur_sort_by_frequency_full_reversal) {
    /* Ascending frequencies force a complete reversal (one large cycle).
     * Asserts the exact sorted order, which also fails if the reorder
     * duplicates one entry and drops another. */
    NcdMatch matches[5];
    memset(matches, 0, sizeof(matches));
    for (int i = 0; i < 5; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path), "C:\\p%d", i);
        matches[i].frequency = (uint32_t)i;   /* p0=0 ... p4=4, ascending */
    }

    heur_sort_by_frequency(matches, 5);

    for (int i = 0; i < 5; i++) {
        char expected[16];
        snprintf(expected, sizeof(expected), "C:\\p%d", 4 - i);
        ASSERT_EQ_STR(expected, matches[i].full_path);
    }

    return 0;
}

/* ================================================================ Test Suite */

void suite_result_output(void) {
    RUN_TEST(write_result_ok_doesnt_crash);
    RUN_TEST(write_result_error_doesnt_crash);
    RUN_TEST(write_result_cancel_doesnt_crash);
    RUN_TEST(result_ok_doesnt_crash);
    RUN_TEST(result_error_doesnt_crash);
    RUN_TEST(result_cancel_doesnt_crash);
    RUN_TEST(heur_sanitize_lowercases);
    RUN_TEST(heur_sanitize_no_lower_preserved);
    RUN_TEST(heur_sanitize_trims_whitespace);
    RUN_TEST(heur_sanitize_handles_empty);
    RUN_TEST(heur_promote_match_moves_to_top);
    RUN_TEST(heur_promote_match_no_match_found);
    RUN_TEST(heur_promote_match_null_or_empty);
    RUN_TEST(heur_sort_by_frequency_two_element_swap);
    RUN_TEST(heur_sort_by_frequency_full_reversal);
}

TEST_MAIN(
    RUN_SUITE(result_output);
)
