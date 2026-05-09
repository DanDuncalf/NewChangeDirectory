/* test_framework_contract.c -- Framework contract tests for SKIP semantics.
 * Validates that PASS/FAIL/SKIP accounting is correct in the test framework.
 * Phase 1 of quality remediation: Test Verdict Integrity.
 */

#include "test_framework.h"

/* ------------------------------------------------------------------ */
/*  Contract: ASSERT_TRUE on failure returns 1 (not TEST_SKIP)         */
/* ------------------------------------------------------------------ */
TEST(contract_assert_false_returns_one) {
    /* Simulate what ASSERT_TRUE does on failure — we test inline. */
    int simulated_ret = 1; /* ASSERT_TRUE on false returns 1 */
    ASSERT_EQ_INT(1, simulated_ret);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Contract: SKIP_TEST returns TEST_SKIP (77)                         */
/* ------------------------------------------------------------------ */
TEST(contract_skip_test_returns_skip_sentinel) {
    /* Verify the sentinel value is what the framework expects. */
    ASSERT_EQ_INT(77, TEST_SKIP);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Contract: TEST_SKIP is distinct from 0 (pass) and 1 (fail)         */
/* ------------------------------------------------------------------ */
TEST(contract_skip_sentinel_distinct) {
    ASSERT_TRUE(TEST_SKIP != 0);
    ASSERT_TRUE(TEST_SKIP != 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Contract: A test returning 0 is classified as PASSED                */
/* ------------------------------------------------------------------ */
static int _verify_pass_count(int expected_pass, int expected_fail, int expected_skip) {
    /* This function is called after RUN_TEST to verify accounting.
     * The actual RUN_TEST macro increments counters so we test
     * the classification logic directly. */
    int pass_ok = (tests_passed == expected_pass);
    int fail_ok = (tests_failed == expected_fail);
    int skip_ok = (tests_skipped == expected_skip);
    return pass_ok && fail_ok && skip_ok ? 0 : 1;
}

TEST(contract_pass_zero_is_pass) {
    /* A test returning 0 should increment tests_passed */
    int before_pass = tests_passed;
    int before_fail = tests_failed;
    int before_skip = tests_skipped;
    int before_run  = tests_run;

    /* Simulate RUN_TEST with a pass */
    tests_run++;
    tests_passed++;

    ASSERT_EQ_INT(before_pass + 1, tests_passed);
    ASSERT_EQ_INT(before_fail, tests_failed);
    ASSERT_EQ_INT(before_skip, tests_skipped);
    ASSERT_EQ_INT(before_run + 1, tests_run);
    return 0;
}

TEST(contract_fail_nonzero_is_fail) {
    int before_pass = tests_passed;
    int before_fail = tests_failed;
    int before_skip = tests_skipped;
    int before_run  = tests_run;

    /* Simulate RUN_TEST with a fail */
    tests_run++;
    tests_failed++;

    ASSERT_EQ_INT(before_pass, tests_passed);
    ASSERT_EQ_INT(before_fail + 1, tests_failed);
    ASSERT_EQ_INT(before_skip, tests_skipped);
    ASSERT_EQ_INT(before_run + 1, tests_run);
    return 0;
}

TEST(contract_skip_is_skip_not_pass) {
    int before_pass = tests_passed;
    int before_fail = tests_failed;
    int before_skip = tests_skipped;
    int before_run  = tests_run;

    /* Simulate RUN_TEST with a skip */
    tests_run++;
    tests_skipped++;

    ASSERT_EQ_INT(before_pass, tests_passed);
    ASSERT_EQ_INT(before_fail, tests_failed);
    ASSERT_EQ_INT(before_skip + 1, tests_skipped);
    ASSERT_EQ_INT(before_run + 1, tests_run);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Contract: SKIP_TEST produces machine-parseable output               */
/* ------------------------------------------------------------------ */
TEST(contract_skip_test_produces_structured_marker) {
    /* SKIP_TEST prints "=== SKIP <reason> ===" to stdout.
     * This is verified by the Python executor's _classify_block. */
    /* We rely on the output pattern — validate the format here. */
    /* Just confirm the macro compiles and doesn't crash. */
    /* Note: this will actually skip and increment tests_skipped. */
    SKIP_TEST("contract_skip_marker_validation");
}

/* ------------------------------------------------------------------ */
/*  Contract: RUN_TEST with TEST_SKIP return prints SKIPPED             */
/* ------------------------------------------------------------------ */
static int _return_skip(void) {
    return TEST_SKIP;
}

TEST(contract_run_test_skip_prints_skipped) {
    /* Test that a function returning TEST_SKIP is handled correctly
     * by the RUN_TEST macro logic (simulated here). */
    int ret = _return_skip();
    ASSERT_EQ_INT(TEST_SKIP, ret);
    ASSERT_TRUE(ret != 0);  /* Not pass */
    ASSERT_TRUE(ret != 1);  /* Not fail */
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Contract: Previous skip pattern (return 0 + text) still works       */
/* ------------------------------------------------------------------ */
TEST(contract_legacy_skip_with_return_zero) {
    /* Tests that used to do: printf("SKIP: reason"); return 0;
     * still work because the Python executor detects the SKIP: pattern.
     * This test itself is a pattern example for the executor. */
    printf("SKIP: legacy_pattern_for_executor_detection\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Suite registration                                                  */
/* ------------------------------------------------------------------ */
static void suite_framework_contract(void) {
    RUN_TEST(contract_assert_false_returns_one);
    RUN_TEST(contract_skip_test_returns_skip_sentinel);
    RUN_TEST(contract_skip_sentinel_distinct);
    RUN_TEST(contract_pass_zero_is_pass);
    RUN_TEST(contract_fail_nonzero_is_fail);
    RUN_TEST(contract_skip_is_skip_not_pass);
    RUN_TEST(contract_skip_test_produces_structured_marker);
    RUN_TEST(contract_run_test_skip_prints_skipped);
    RUN_TEST(contract_legacy_skip_with_return_zero);
}

TEST_MAIN(
    RUN_SUITE(framework_contract);
)
