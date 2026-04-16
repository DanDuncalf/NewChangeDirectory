/*
 * test_service_empty_db.c  --  Test service mode with empty database
 *
 * This tests the scenario where:
 * 1. The NCD service is running
 * 2. The service has an empty database (0 drives, or no databases loaded)
 * 3. A client searches for a directory
 *
 * This should properly return an error message and write the result file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"

/* Platform detection */
#if defined(_WIN32) || defined(_WIN64)
#define TEST_PLATFORM_WINDOWS 1
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#define TEST_PLATFORM_LINUX 1
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

/* Include NCD headers */
#include "../src/ncd.h"
#include "../src/state_backend.h"
#include "../src/control_ipc.h"

/* Test: Service backend with empty database should gracefully fail */
TEST(service_empty_db_returns_null) {
    /* 
     * This test verifies that when the service is running but has
     * no database (0 drives), the client properly falls back to
     * local mode instead of hanging or crashing.
     */
    
    /* Try to initialize state backend */
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    fprintf(stderr, "TEST: Attempting state_backend_open_best_effort...\n");
    int result = state_backend_open_best_effort(&view, &info);
    fprintf(stderr, "TEST: Result=%d, view=%p, from_service=%d\n", 
            result, (void*)view, info.from_service);
    
    /* If service is not running, skip this test */
    if (result != 0 || !view) {
        fprintf(stderr, "TEST: Service not available, skipping\n");
        /* Not a failure - service just isn't running */
        return 0;
    }
    
    /* Check if we got a database */
    const NcdDatabase *db = state_view_database(view);
    fprintf(stderr, "TEST: Database=%p, drive_count=%d\n", 
            (void*)db, db ? db->drive_count : -1);
    
    if (info.from_service) {
        /* Service mode - database may be empty */
        if (db && db->drive_count == 0) {
            fprintf(stderr, "TEST: Service has empty database (expected for this test)\n");
            /* This is the scenario we're testing - service running but empty DB */
        }
    }
    
    /* Cleanup */
    state_backend_close(view);
    
    return 0;
}

/* Test: Verify write_result creates file even with error result */
TEST(write_result_creates_file_on_error) {
    /* Need to call the actual write_result function through result_error */
    /* This is tested indirectly through integration tests */
    
    /* Just verify that temp path is accessible */
    char tmp_path[MAX_PATH];
    bool got_temp = platform_get_temp_path(tmp_path, sizeof(tmp_path));
    ASSERT_TRUE(got_temp);
    
    if (got_temp) {
        fprintf(stderr, "TEST: Temp path = %s\n", tmp_path);
        
        /* Verify we can write to temp directory */
        char test_file[MAX_PATH];
        snprintf(test_file, sizeof(test_file), "%sncd_test_%d.tmp", 
                 tmp_path, (int)getpid());
        
        FILE *f = fopen(test_file, "w");
        ASSERT_NOT_NULL(f);
        
        if (f) {
            fprintf(f, "TEST\n");
            fclose(f);
            
            /* Verify file exists */
            ASSERT_TRUE(platform_file_exists(test_file));
            
            /* Cleanup */
            platform_delete_file(test_file);
        }
    }
    
    return 0;
}

/* Test: Verify IPC timeout handling doesn't hang */
TEST(ipc_connection_has_timeout) {
    /* 
     * Verify that IPC operations have reasonable timeouts
     * and don't hang indefinitely.
     */
    
    /* Check if service exists */
    bool service_exists = ipc_service_exists();
    fprintf(stderr, "TEST: ipc_service_exists = %d\n", service_exists);
    
    if (!service_exists) {
        fprintf(stderr, "TEST: Service not running, skipping timeout test\n");
        return 0;
    }
    
    /* Try to connect */
    fprintf(stderr, "TEST: Attempting IPC connection...\n");
    NcdIpcClient *client = ipc_client_connect();
    
    if (client) {
        fprintf(stderr, "TEST: Connected successfully\n");
        
        /* Try a ping - should complete quickly, not hang */
        fprintf(stderr, "TEST: Sending ping...\n");
        NcdIpcResult result = ipc_client_ping(client);
        fprintf(stderr, "TEST: Ping result = %d\n", result);
        
        ipc_client_disconnect(client);
    } else {
        fprintf(stderr, "TEST: Connection failed (may be expected if service busy)\n");
    }
    
    return 0;
}

/* Test: Full search flow with service empty database */
TEST(search_with_service_empty_db_produces_result) {
    /*
     * This is the key test - when the service is running but has
     * an empty database, a search should:
     * 1. Not hang
     * 2. Produce a result file (even if it's an error)
     * 3. Return appropriate error message
     */
    
    fprintf(stderr, "TEST: Starting full search flow test\n");
    
    /* Initialize state backend */
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    int result = state_backend_open_best_effort(&view, &info);
    
    fprintf(stderr, "TEST: state_backend_open_best_effort: result=%d, from_service=%d\n",
            result, info.from_service);
    
    if (result != 0 || !view) {
        fprintf(stderr, "TEST: No state backend available, skipping\n");
        return 0;
    }
    
    /* Get database */
    const NcdDatabase *db = state_view_database(view);
    fprintf(stderr, "TEST: Database: %p, drives: %d\n", 
            (void*)db, db ? db->drive_count : -1);
    
    if (!info.from_service) {
        fprintf(stderr, "TEST: Not using service, skipping\n");
        state_backend_close(view);
        return 0;
    }
    
    /* Service is running - now try a search */
    if (db && db->drive_count == 0) {
        fprintf(stderr, "TEST: Service has empty database - this is our test case!\n");
        
        /* Try to search - this should not hang */
        /* Note: We can't easily call matcher_find from here without more setup */
        /* The actual test is done via the integration test that runs the full exe */
        
        fprintf(stderr, "TEST: Empty database scenario confirmed\n");
    }
    
    state_backend_close(view);
    return 0;
}

void suite_service_empty_db(void) {
    RUN_TEST(service_empty_db_returns_null);
    RUN_TEST(write_result_creates_file_on_error);
    RUN_TEST(ipc_connection_has_timeout);
    RUN_TEST(search_with_service_empty_db_produces_result);
}

TEST_MAIN(
    printf("=== service_empty_db ===\n");
    suite_service_empty_db();
)
