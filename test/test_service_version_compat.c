/*
 * test_service_version_compat.c  --  Service version compatibility tests
 *
 * Tests:
 * - Version check between client and service
 * - Version mismatch detection
 * - Auto-shutdown of mismatched service
 * - Cross-version compatibility scenarios
 *
 * These tests verify that the NCD client and service handle version
 * mismatches gracefully.
 */

#include "test_framework.h"
#include "service_test_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------- test utilities     */

#define SERVICE_TIMEOUT 10

/* Ensure service is running */
static bool ensure_service_running(void) {
    if (ipc_service_exists()) {
        return true;
    }
    
    if (!service_executable_exists()) {
        return false;
    }
    
    run_service_command("start", NULL, 0);
    return wait_for_service_state(true, SERVICE_TIMEOUT);
}

/* --------------------------------------------------------- version check tests */

TEST(version_check_when_service_running) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    /* Start service */
    if (!ensure_service_running()) {
        SKIP_TEST("Could not start service");
    }
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Check version */
    NcdIpcVersionCheckResult result;
    NcdIpcResult ipc_result = ipc_client_check_version(client, NCD_APP_VERSION, 
                                                        __DATE__ " " __TIME__, &result);
    
    /* Should succeed and versions should match */
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_result);
    ASSERT_TRUE(result.versions_match);
    ASSERT_FALSE(result.service_was_stopped);
    ASSERT_STR_CONTAINS(result.message, "match");
    
    ipc_client_disconnect(client);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

TEST(version_check_gets_service_version_info) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    if (!ensure_service_running()) {
        SKIP_TEST("Could not start service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get version info directly */
    NcdIpcVersionInfo info;
    NcdIpcResult ipc_result = ipc_client_get_version(client, &info);
    
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_result);
    ASSERT_TRUE(strlen(info.app_version) > 0);
    ASSERT_TRUE(strlen(info.build_stamp) > 0);
    ASSERT_EQ_INT(NCD_IPC_VERSION, info.protocol_version);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

TEST(version_check_mismatch_triggers_shutdown) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    if (!ensure_service_running()) {
        SKIP_TEST("Could not start service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Use a fake version that doesn't match */
    NcdIpcVersionCheckResult result;
    (void)ipc_client_check_version(client, "0.0.0", 
                                    "Jan 01 2020 00:00:00", &result);
    
    /* Should report mismatch */
    ASSERT_FALSE(result.versions_match);
    /* Service should have been stopped or stop was attempted */
    ASSERT_TRUE(result.service_was_stopped || !ipc_service_exists());
    ASSERT_STR_CONTAINS(result.message, "mismatch");
    
    ipc_client_disconnect(client);
    
    /* Give service time to shut down */
    platform_sleep_ms(500);
    ensure_service_stopped();
    return 0;
}

TEST(version_check_result_populated_correctly) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    if (!ensure_service_running()) {
        SKIP_TEST("Could not start service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    const char *test_client_ver = "1.2.3";
    const char *test_client_build = "Test Build 123";
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    
    ipc_client_check_version(client, test_client_ver, test_client_build, &result);
    
    /* Result should contain client version info */
    ASSERT_EQ_STR(test_client_ver, result.client_version);
    ASSERT_EQ_STR(test_client_build, result.client_build);
    
    /* Result should contain service version info */
    ASSERT_TRUE(strlen(result.service_version) > 0);
    ASSERT_TRUE(strlen(result.service_build) > 0);
    
    /* Message should be populated */
    ASSERT_TRUE(strlen(result.message) > 0);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

TEST(version_check_invalid_params_fails) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    if (!ensure_service_running()) {
        SKIP_TEST("Could not start service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    
    /* NULL client_version should fail */
    NcdIpcResult ipc_result = ipc_client_check_version(client, NULL, "build", &result);
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, ipc_result);
    
    /* NULL client_build should fail */
    ipc_result = ipc_client_check_version(client, "version", NULL, &result);
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, ipc_result);
    
    /* NULL result should fail */
    ipc_result = ipc_client_check_version(client, "version", "build", NULL);
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, ipc_result);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

TEST(protocol_version_matches_expected) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    if (!ensure_service_running()) {
        SKIP_TEST("Could not start service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionInfo info;
    NcdIpcResult ipc_result = ipc_client_get_version(client, &info);
    
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_result);
    /* Protocol version should be 4 (current version) */
    ASSERT_EQ_INT(4, info.protocol_version);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_version_compat(void) {
    RUN_TEST(version_check_when_service_running);
    RUN_TEST(version_check_gets_service_version_info);
    RUN_TEST(version_check_mismatch_triggers_shutdown);
    RUN_TEST(version_check_result_populated_correctly);
    RUN_TEST(version_check_invalid_params_fails);
    RUN_TEST(protocol_version_matches_expected);
}

TEST_MAIN(
    suite_version_compat();
)
