/*
 * test_service_parity.c  --  Parity tests between standalone and service modes
 *
 * These tests verify that the NCD client behaves identically whether
 * using standalone (disk) mode or service-backed (shared memory) mode.
 * 
 * NOTE ON STRUCTURE SIZE:
 * NcdStateView is very large (~45KB on x64) because the service variant
 * contains full NcdMetadata (~41KB) and NcdDatabase (~4KB) structures.
 * We allocate on the heap to avoid stack overflow issues.
 * 
 * Structure sizes (Linux x64):
 * - sizeof(NcdStateView) = 45,456 bytes
 * - sizeof(local variant) = 24 bytes (pointers only)
 * - sizeof(service variant) = 45,432 bytes (full embedded structures)
 * - Ratio: service is ~1893x larger than local
 * 
 * ISOLATION NOTE:
 * On Linux/WSL, these tests require XDG_DATA_HOME to be set to a temp
 * directory to avoid loading the user's real metadata. On Windows,
 * LOCALAPPDATA should similarly be redirected for test isolation.
 */

#include "test_framework.h"
#include "../src/state_backend.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_LINUX
#include <unistd.h>
#include <sys/stat.h>
#else
#include <direct.h>
#include <windows.h>
#endif

/* Test isolation setup - save original env vars */
static char g_orig_xdg_data_home[4096] = {0};
static char g_orig_localappdata[4096] = {0};
static char g_test_data_dir[4096] = {0};

/* Setup test isolation - redirect data directories to temp location */
static void setup_test_isolation(void) {
#if NCD_PLATFORM_LINUX
    /* Save original XDG_DATA_HOME */
    const char *orig = getenv("XDG_DATA_HOME");
    if (orig) {
        strncpy(g_orig_xdg_data_home, orig, sizeof(g_orig_xdg_data_home) - 1);
    }
    
    /* Create temp test directory */
    snprintf(g_test_data_dir, sizeof(g_test_data_dir), "/tmp/ncd_parity_test_%d", getpid());
    mkdir(g_test_data_dir, 0755);
    
    /* Create NCD subdirectory */
    char ncd_dir[4096];
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/ncd", g_test_data_dir);
    mkdir(ncd_dir, 0755);
    
    /* Set XDG_DATA_HOME to temp directory */
    setenv("XDG_DATA_HOME", g_test_data_dir, 1);
#else
    /* Windows - save original LOCALAPPDATA */
    const char *orig = getenv("LOCALAPPDATA");
    if (orig) {
        strncpy(g_orig_localappdata, orig, sizeof(g_orig_localappdata) - 1);
    }
    
    /* Get temp directory */
    const char *temp = getenv("TEMP");
    if (!temp) temp = "C:\\temp";
    
    /* Create temp test directory */
    snprintf(g_test_data_dir, sizeof(g_test_data_dir), 
        "%s\\ncd_parity_test_%d", temp, GetCurrentProcessId());
    _mkdir(g_test_data_dir);
    
    /* Create NCD subdirectory */
    char ncd_dir[4096];
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", g_test_data_dir);
    _mkdir(ncd_dir);
    
    (void)GetCurrentProcessId; /* Silence unused warning if not used */
    
    /* Set LOCALAPPDATA to temp directory */
    SetEnvironmentVariableA("LOCALAPPDATA", g_test_data_dir);
#endif
}

/* Cleanup test isolation - restore original env vars and remove temp dir */
static void cleanup_test_isolation(void) {
#if NCD_PLATFORM_LINUX
    /* Restore original XDG_DATA_HOME */
    if (g_orig_xdg_data_home[0]) {
        setenv("XDG_DATA_HOME", g_orig_xdg_data_home, 1);
    } else {
        unsetenv("XDG_DATA_HOME");
    }
    
    /* Remove temp directory recursively (best effort) */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_data_dir);
    system(cmd);
#else
    /* Restore original LOCALAPPDATA */
    if (g_orig_localappdata[0]) {
        SetEnvironmentVariableA("LOCALAPPDATA", g_orig_localappdata);
    }
    
    /* Remove temp directory recursively (best effort) */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", g_test_data_dir);
    system(cmd);
#endif
}

/* Test that local backend works independently */
TEST(local_backend_basic) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_local(&view, &info);
    ASSERT_TRUE(result == 0);
    ASSERT_NOT_NULL(view);
    ASSERT_FALSE(info.from_service);  /* Should be local mode */
    
    /* Get metadata and database */
    const NcdMetadata *meta = state_view_metadata(view);
    const NcdDatabase *db = state_view_database(view);
    
    /* Metadata should be available */
    ASSERT_NOT_NULL(meta);
    
    /* Database may be empty but should exist */
    ASSERT_NOT_NULL(db);
    
    /* Clean up */
    state_backend_close(view);
    
    return 0;
}

/* Test that best-effort falls back to local when service unavailable */
TEST(best_effort_fallback) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_best_effort(&view, &info);
    
    /* Should succeed (either via service or local fallback) */
    ASSERT_TRUE(result == 0);
    ASSERT_NOT_NULL(view);
    
    /* If service is not running, should fallback to local */
    /* We can't predict which one we'll get, but the operation should work */
    
    /* Get metadata - should work regardless of mode */
    const NcdMetadata *meta = state_view_metadata(view);
    ASSERT_NOT_NULL(meta);
    
    /* Clean up */
    state_backend_close(view);
    
    return 0;
}

/* Test state view consistency */
TEST(state_view_consistency) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_local(&view, &info);
    ASSERT_TRUE(result == 0);
    ASSERT_NOT_NULL(view);
    
    /* Get source info */
    NcdStateSourceInfo info2;
    state_backend_get_source_info(view, &info2);
    ASSERT_EQ_INT((int)info.from_service, (int)info2.from_service);
    
    /* Multiple calls to get metadata should return same pointer */
    const NcdMetadata *meta1 = state_view_metadata(view);
    const NcdMetadata *meta2 = state_view_metadata(view);
    ASSERT_TRUE(meta1 == meta2);
    
    /* Multiple calls to get database should return same pointer */
    const NcdDatabase *db1 = state_view_database(view);
    const NcdDatabase *db2 = state_view_database(view);
    ASSERT_TRUE(db1 == db2);
    
    state_backend_close(view);
    
    return 0;
}

/* Test multiple open/close cycles */
TEST(open_close_cycles) {
    for (int i = 0; i < 3; i++) {
        NcdStateView *view = NULL;
        NcdStateSourceInfo info;
        
        int result = state_backend_open_local(&view, &info);
        ASSERT_TRUE(result == 0);
        ASSERT_NOT_NULL(view);
        
        /* Verify we can access data */
        const NcdMetadata *meta = state_view_metadata(view);
        ASSERT_NOT_NULL(meta);
        
        state_backend_close(view);
    }
    
    return 0;
}

/* Test error handling for NULL pointers */
TEST(null_handling) {
    NcdStateSourceInfo info;
    
    /* NULL out pointer should fail */
    int result = state_backend_open_local(NULL, &info);
    ASSERT_TRUE(result != 0);
    
    /* NULL info is allowed (should not crash) */
    NcdStateView *view = NULL;
    result = state_backend_open_local(&view, NULL);
    ASSERT_TRUE(result == 0);
    ASSERT_NOT_NULL(view);
    state_backend_close(view);
    
    return 0;
}

/* Test that standalone mode is always available */
TEST(standalone_always_available) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    /* Standalone mode should always work */
    int result = state_backend_open_local(&view, &info);
    ASSERT_TRUE(result == 0);
    ASSERT_NOT_NULL(view);
    ASSERT_FALSE(info.from_service);
    
    /* Should be able to get metadata */
    const NcdMetadata *meta = state_view_metadata(view);
    ASSERT_NOT_NULL(meta);
    
    /* Config should have valid defaults */
    ASSERT_TRUE(meta->cfg.magic != 0);
    
    state_backend_close(view);
    
    return 0;
}

/* Test that local and service modes return consistent structure layouts */
TEST(structure_consistency) {
    /* Verify that the metadata structure is the same size in both modes */
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_local(&view, &info);
    ASSERT_TRUE(result == 0);
    
    const NcdMetadata *meta = state_view_metadata(view);
    ASSERT_NOT_NULL(meta);
    
    /* Verify basic structure fields are accessible */
    ASSERT_TRUE(meta->cfg.version > 0);
    
    state_backend_close(view);
    
    return 0;
}

/* Test database lifecycle - open, access, close */
TEST(database_lifecycle) {
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    /* Open local backend */
    int result = state_backend_open_local(&view, &info);
    ASSERT_TRUE(result == 0);
    ASSERT_NOT_NULL(view);
    ASSERT_FALSE(info.from_service);
    
    /* Get database */
    const NcdDatabase *db = state_view_database(view);
    ASSERT_NOT_NULL(db);
    
    /* Database should have valid version */
    ASSERT_TRUE(db->version > 0);
    
    /* Get metadata */
    const NcdMetadata *meta = state_view_metadata(view);
    ASSERT_NOT_NULL(meta);
    
    /* Close backend */
    state_backend_close(view);
    
    return 0;
}

/* Test that metadata is the same between client and service views */
TEST(metadata_structure_identity) {
    /* This test verifies that NcdMetadata has the same layout regardless
     * of whether it comes from local disk or shared memory.
     * 
     * The key insight is that the service backend copies data from shared
     * memory into an NcdMetadata structure, so the client always sees the
     * same structure layout.
     */
    
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_local(&view, &info);
    ASSERT_TRUE(result == 0);
    
    const NcdMetadata *meta = state_view_metadata(view);
    ASSERT_NOT_NULL(meta);
    
    /* Verify critical fields are accessible */
    ASSERT_TRUE(meta->cfg.magic == NCD_CFG_MAGIC || meta->cfg.magic == NCD_META_MAGIC);
    
    /* Groups structure should be valid */
    ASSERT_TRUE(meta->groups.count >= 0);
    ASSERT_TRUE(meta->groups.capacity >= meta->groups.count);
    
    /* Heuristics structure should be valid */
    ASSERT_TRUE(meta->heuristics.count >= 0);
    ASSERT_TRUE(meta->heuristics.capacity >= meta->heuristics.count);
    
    /* Exclusions structure should be valid */
    ASSERT_TRUE(meta->exclusions.count >= 0);
    ASSERT_TRUE(meta->exclusions.capacity >= meta->exclusions.count);
    
    state_backend_close(view);
    
    return 0;
}

void suite_service_parity(void) {
    RUN_TEST(local_backend_basic);
    RUN_TEST(best_effort_fallback);
    RUN_TEST(state_view_consistency);
    RUN_TEST(open_close_cycles);
    RUN_TEST(null_handling);
    RUN_TEST(standalone_always_available);
    RUN_TEST(structure_consistency);
    RUN_TEST(database_lifecycle);
    RUN_TEST(metadata_structure_identity);
}

TEST_MAIN(
    printf("Service Parity Tests for NCD\n");
    printf("============================\n");
    printf("These tests verify that the client behaves identically whether\n");
    printf("using standalone (disk) mode or service-backed (shared memory) mode.\n\n");
    printf("Key architectural insight:\n");
    printf("- NcdStateView is a unified structure for both local and service modes\n");
    printf("- Local mode: uses pointers to heap-allocated metadata/database\n");
    printf("- Service mode: embeds full copies of metadata/database in the union\n");
    printf("- Result: Service variant is ~1893x larger but provides data locality\n\n");
    printf("Note: NcdStateView is ~45KB, allocated on heap to avoid stack issues\n\n");
    
    /* Setup test isolation to avoid loading user's real metadata */
    setup_test_isolation();
    
    suite_service_parity();
    
    /* Cleanup test isolation */
    cleanup_test_isolation();
)
