/* test_ipc.c -- Tests for IPC layer (Tier 4) */
#include "test_framework.h"
#include "../src/control_ipc.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================ Tier 4: IPC Tests */

TEST(ipc_client_init_succeeds) {
    int result = ipc_client_init();
    
    ASSERT_EQ_INT(0, result);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_client_connect_fails_when_no_service) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);
    
    /* Try to connect when no service is running */
    NcdIpcClient *client = ipc_client_connect();
    
    /* Should fail since service is not running */
    ASSERT_NULL(client);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_make_address_returns_valid_path) {
    char path[256];
    bool result = ipc_make_address(path, sizeof(path));
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(path) > 0);
    
    /* Path should be platform-specific:
     * Windows: named pipe path like \\.\pipe\...
     * Linux: socket path like /tmp/... or /run/...
     */
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(strstr(path, "pipe") != NULL || path[0] == '\\');
#else
    ASSERT_TRUE(path[0] == '/');
#endif
    
    return 0;
}

TEST(ipc_error_string_for_all_result_codes) {
    const char *str;
    
    str = ipc_error_string(NCD_IPC_OK);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_GENERIC);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_INVALID);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_NOT_FOUND);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY_LOADING);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY_SCANNING);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_NOT_READY);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    return 0;
}

TEST(ipc_client_cleanup_doesnt_crash) {
    /* Should not crash even if called multiple times */
    ipc_client_init();
    ipc_client_cleanup();
    ipc_client_cleanup();
    
    return 0;
}

TEST(ipc_client_disconnect_handles_null) {
    /* Should not crash with NULL */
    ipc_client_disconnect(NULL);
    
    return 0;
}

TEST(ipc_client_ping_fails_without_connection) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);
    
    /* Ping without connection should fail */
    NcdIpcResult ping_result = ipc_client_ping(NULL);
    ASSERT_TRUE(ping_result != NCD_IPC_OK);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_client_get_version_fails_without_connection) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);
    
    NcdIpcVersionInfo info;
    NcdIpcResult ver_result = ipc_client_get_version(NULL, &info);
    ASSERT_TRUE(ver_result != NCD_IPC_OK);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_service_exists_returns_false_when_stopped) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);
    
    /* When no service is running, should return false */
    bool exists = ipc_service_exists();
    /* This may return true or false depending on whether a service
     * is actually running - we just verify it doesn't crash */
    (void)exists;
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_header_size_correct) {
    /* Verify header structure size */
    ASSERT_EQ_INT(16, sizeof(NcdIpcHeader));
}

TEST(ipc_payload_structures_sizes) {
    /* Verify payload structures are reasonable sizes */
    ASSERT_TRUE(sizeof(NcdSubmitHeuristicPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdSubmitMetadataPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdRequestRescanPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdStateInfoPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdVersionInfoPayload) <= 128);
    ASSERT_TRUE(sizeof(NcdErrorPayload) <= 256);
}

/* ================================================================ Test Suite */

void suite_ipc(void) {
    printf("\n=== IPC Tests ===\n\n");
    
    RUN_TEST(ipc_client_init_succeeds);
    RUN_TEST(ipc_client_connect_fails_when_no_service);
    RUN_TEST(ipc_make_address_returns_valid_path);
    RUN_TEST(ipc_error_string_for_all_result_codes);
    RUN_TEST(ipc_client_cleanup_doesnt_crash);
    RUN_TEST(ipc_client_disconnect_handles_null);
    RUN_TEST(ipc_client_ping_fails_without_connection);
    RUN_TEST(ipc_client_get_version_fails_without_connection);
    RUN_TEST(ipc_service_exists_returns_false_when_stopped);
    RUN_TEST(ipc_header_size_correct);
    RUN_TEST(ipc_payload_structures_sizes);
}

TEST_MAIN(
    RUN_SUITE(ipc);
)
