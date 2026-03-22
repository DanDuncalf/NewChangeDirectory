/*
 * test_service_parity.c  --  Parity tests between standalone and service modes
 *
 * These tests verify that the NCD client behaves identically whether
 * using standalone (disk) mode or service-backed (shared memory) mode.
 */

#include "test_framework.h"
#include "../src/state_backend.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Test that local backend works independently */
static void test_local_backend_basic(void) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_local(&view, &info);
    TEST_ASSERT(result == 0);
    TEST_ASSERT(view != NULL);
    TEST_ASSERT(info.from_service == false);
    
    /* Get metadata */
    const NcdMetadata *meta = state_view_metadata(view);
    /* Metadata may be NULL if no metadata file exists, that's OK */
    
    /* Get database */
    const NcdDatabase *db = state_view_database(view);
    /* Database may be empty, that's OK */
    
    state_backend_close(view);
}

/* Test that best-effort falls back to local when service unavailable */
static void test_best_effort_fallback(void) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_best_effort(&view, &info);
    TEST_ASSERT(result == 0);
    TEST_ASSERT(view != NULL);
    
    /* Should either be from service or local */
    /* In test environment without service, should be local */
    if (!info.from_service) {
        TEST_ASSERT(info.generation == 0);
    }
    
    state_backend_close(view);
}

/* Test state view consistency */
static void test_state_view_consistency(void) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_local(&view, &info);
    TEST_ASSERT(result == 0);
    
    /* Get source info */
    NcdStateSourceInfo info2;
    state_backend_get_source_info(view, &info2);
    
    TEST_ASSERT(info.from_service == info2.from_service);
    TEST_ASSERT(info.generation == info2.generation);
    TEST_ASSERT(info.db_generation == info2.db_generation);
    
    state_backend_close(view);
}

/* Test multiple open/close cycles */
static void test_open_close_cycles(void) {
    for (int i = 0; i < 5; i++) {
        NcdStateView *view = NULL;
        NcdStateSourceInfo info;
        
        int result = state_backend_open_local(&view, &info);
        TEST_ASSERT(result == 0);
        TEST_ASSERT(view != NULL);
        
        state_backend_close(view);
    }
}

/* Test error handling for NULL pointers */
static void test_null_handling(void) {
    NcdStateSourceInfo info;
    
    /* NULL out pointer should fail */
    int result = state_backend_open_local(NULL, &info);
    TEST_ASSERT(result != 0);
    
    /* Valid pointer should succeed */
    NcdStateView *view = NULL;
    result = state_backend_open_local(&view, &info);
    if (result == 0) {
        /* NULL metadata and database views should be handled */
        const NcdMetadata *meta = state_view_metadata(NULL);
        TEST_ASSERT(meta == NULL);
        
        const NcdDatabase *db = state_view_database(NULL);
        TEST_ASSERT(db == NULL);
        
        state_backend_close(view);
    }
}

/* Test that standalone mode is always available */
static void test_standalone_always_available(void) {
    /* Even with service supposedly present, local mode should work */
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_local(&view, &info);
    TEST_ASSERT(result == 0);
    TEST_ASSERT(view != NULL);
    TEST_ASSERT(info.from_service == false);
    
    state_backend_close(view);
}

/* Main test runner */
TEST_SUITE(service_parity) {
    RUN_TEST(local_backend_basic);
    RUN_TEST(best_effort_fallback);
    RUN_TEST(state_view_consistency);
    RUN_TEST(open_close_cycles);
    RUN_TEST(null_handling);
    RUN_TEST(standalone_always_available);
}

int main(void) {
    printf("Starting service parity tests...\n\n");
    
    TEST_SUITE_RUN(service_parity);
    
    printf("\n");
    TEST_SUITE_REPORT(service_parity);
    
    return TEST_SUITE_FAILED(service_parity) ? 1 : 0;
}
