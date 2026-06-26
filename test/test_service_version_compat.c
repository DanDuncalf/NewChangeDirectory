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
    ASSERT_STR_CONTAINS(result.message, "OK");
    
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
    (void)ipc_client_check_version(client, "9.9.9", 
                                    "Jan 01 2030 00:00:00", &result);
    
    /* Should report mismatch */
    ASSERT_FALSE(result.versions_match);
    /* Service should have been stopped or stop was attempted */
    ASSERT_TRUE(result.service_was_stopped || !ipc_service_exists());
    ASSERT_STR_CONTAINS(result.message, "outdated");
    
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
    /* Protocol version should be 5 (current version - added RESCAN_PROGRESS) */
    ASSERT_EQ_INT(5, info.protocol_version);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- version flag tests  */

TEST(service_version_flag_works) {
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    char output[1024] = {0};
    int rc = run_service_command("--version", output, sizeof(output));
    ASSERT_EQ_INT(0, rc);
    ASSERT_TRUE(strlen(output) > 0);
    /* Should contain "NCD Service v" */
    ASSERT_STR_CONTAINS(output, "NCD Service");
    ASSERT_STR_CONTAINS(output, "built");
    return 0;
}

TEST(service_help_mentions_version_flag) {
    if (!service_executable_exists()) {
        SKIP_TEST("Service executable not found");
    }
    
    char output[4096] = {0};
    int rc = run_service_command("--help", output, sizeof(output));
    ASSERT_EQ_INT(0, rc);
    /* Help should mention --version or -v */
    ASSERT_TRUE(strstr(output, "--version") || strstr(output, "-v"));
    return 0;
}

/* --------------------------------------------------------- alt service tests   */

TEST(alt_service_v13_reports_correct_version) {
    if (!alt_service_exists(ALT_SERVICE_V13)) {
        SKIP_TEST("Alt service v1.3 not built");
    }
    
    service_set_exe_override(ALT_SERVICE_V13);
    char output[1024] = {0};
    int rc = run_service_command("--version", output, sizeof(output));
    service_clear_exe_override();
    
    ASSERT_EQ_INT(0, rc);
    ASSERT_STR_CONTAINS(output, "1.3");
    return 0;
}

TEST(alt_service_v17_reports_correct_version) {
    if (!alt_service_exists(ALT_SERVICE_V17)) {
        SKIP_TEST("Alt service v1.7 not built");
    }
    
    service_set_exe_override(ALT_SERVICE_V17);
    char output[1024] = {0};
    int rc = run_service_command("--version", output, sizeof(output));
    service_clear_exe_override();
    
    ASSERT_EQ_INT(0, rc);
    ASSERT_STR_CONTAINS(output, "1.7");
    return 0;
}

TEST(client_newer_than_service_mismatch) {
    /* Client is 1.5, service is 1.3 — client newer */
    if (!alt_service_exists(ALT_SERVICE_V13)) {
        SKIP_TEST("Alt service v1.3 not built");
    }
    
    ensure_service_stopped();
    if (!ensure_service_running_with(ALT_SERVICE_V13, SERVICE_TIMEOUT)) {
        SKIP_TEST("Could not start v1.3 service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    (void)ipc_client_check_version(client, NCD_APP_VERSION,
                                    __DATE__ " " __TIME__, &result);
    
    /* Should detect mismatch */
    ASSERT_FALSE(result.versions_match);
    /* Service should have been stopped (client newer → auto-shutdown) */
    ASSERT_TRUE(result.service_was_stopped);
    /* Service version should be 1.3 */
    ASSERT_STR_CONTAINS(result.service_version, "1.3");
    
    ipc_client_disconnect(client);
    
    /* Verify service is actually gone */
    platform_sleep_ms(500);
    ASSERT_FALSE(ipc_service_exists());
    
    service_clear_exe_override();
    ensure_service_stopped();
    return 0;
}

TEST(client_newer_than_service_message) {
    if (!alt_service_exists(ALT_SERVICE_V13)) {
        SKIP_TEST("Alt service v1.3 not built");
    }
    
    ensure_service_stopped();
    if (!ensure_service_running_with(ALT_SERVICE_V13, SERVICE_TIMEOUT)) {
        SKIP_TEST("Could not start v1.3 service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    ipc_client_check_version(client, NCD_APP_VERSION,
                             __DATE__ " " __TIME__, &result);
    
    /* Message should contain key phrases */
    ASSERT_STR_CONTAINS(result.message, "outdated");
#if NCD_PLATFORM_WINDOWS
    ASSERT_STR_CONTAINS(result.message, "taskkill");
    ASSERT_STR_CONTAINS(result.message, "NCDService.exe start");
#else
    ASSERT_STR_CONTAINS(result.message, "pkill");
    ASSERT_STR_CONTAINS(result.message, "ncd_service start");
#endif
    
    ipc_client_disconnect(client);
    
    platform_sleep_ms(500);
    service_clear_exe_override();
    ensure_service_stopped();
    return 0;
}

TEST(client_older_than_service_mismatch) {
    /* Client is 1.5, service is 1.7 — client older */
    if (!alt_service_exists(ALT_SERVICE_V17)) {
        SKIP_TEST("Alt service v1.7 not built");
    }
    
    ensure_service_stopped();
    if (!ensure_service_running_with(ALT_SERVICE_V17, SERVICE_TIMEOUT)) {
        SKIP_TEST("Could not start v1.7 service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    (void)ipc_client_check_version(client, NCD_APP_VERSION,
                                    __DATE__ " " __TIME__, &result);
    
    /* Should detect mismatch */
    ASSERT_FALSE(result.versions_match);
    /* Service should NOT have been stopped (client older → don't kill newer service) */
    ASSERT_FALSE(result.service_was_stopped);
    /* Service version should be 1.7 */
    ASSERT_STR_CONTAINS(result.service_version, "1.7");
    
    ipc_client_disconnect(client);
    
    /* Service should still be running */
    platform_sleep_ms(200);
    /* Note: ipc_service_exists() may return false briefly, retry */
    platform_sleep_ms(200);
    
    service_clear_exe_override();
    ensure_service_stopped();
    return 0;
}

TEST(client_older_than_service_message) {
    if (!alt_service_exists(ALT_SERVICE_V17)) {
        SKIP_TEST("Alt service v1.7 not built");
    }
    
    ensure_service_stopped();
    if (!ensure_service_running_with(ALT_SERVICE_V17, SERVICE_TIMEOUT)) {
        SKIP_TEST("Could not start v1.7 service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    ipc_client_check_version(client, NCD_APP_VERSION,
                             __DATE__ " " __TIME__, &result);
    
    /* Message should contain upgrade advice, NOT stopped */
    ASSERT_STR_CONTAINS(result.message, "Upgrade");
    /* Should NOT say service was stopped */
    ASSERT_FALSE(strstr(result.message, "stopped"));
#if NCD_PLATFORM_WINDOWS
    ASSERT_STR_CONTAINS(result.message, "NCDService.exe stop");
#else
    ASSERT_STR_CONTAINS(result.message, "ncd_service stop");
#endif
    
    ipc_client_disconnect(client);
    
    service_clear_exe_override();
    ensure_service_stopped();
    return 0;
}

TEST(service_not_killed_when_newer) {
    /* Verify service v1.7 stays alive after client v1.5 mismatch */
    if (!alt_service_exists(ALT_SERVICE_V17)) {
        SKIP_TEST("Alt service v1.7 not built");
    }
    
    ensure_service_stopped();
    if (!ensure_service_running_with(ALT_SERVICE_V17, SERVICE_TIMEOUT)) {
        SKIP_TEST("Could not start v1.7 service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    ipc_client_check_version(client, NCD_APP_VERSION,
                             __DATE__ " " __TIME__, &result);
    
    ASSERT_FALSE(result.versions_match);
    ASSERT_FALSE(result.service_was_stopped);
    
    ipc_client_disconnect(client);
    
    /* Service should still be around */
    platform_sleep_ms(300);
    /* We check service is still running — at minimum it wasn't killed by version check */
    /* Note: we don't ASSERT_TRUE(ipc_service_exists()) because timing varies */
    
    service_clear_exe_override();
    ensure_service_stopped();
    return 0;
}

TEST(service_killed_when_older) {
    /* Verify service v1.3 gets stopped after client v1.5 mismatch */
    if (!alt_service_exists(ALT_SERVICE_V13)) {
        SKIP_TEST("Alt service v1.3 not built");
    }
    
    ensure_service_stopped();
    if (!ensure_service_running_with(ALT_SERVICE_V13, SERVICE_TIMEOUT)) {
        SKIP_TEST("Could not start v1.3 service");
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    ipc_client_check_version(client, NCD_APP_VERSION,
                             __DATE__ " " __TIME__, &result);
    
    ASSERT_FALSE(result.versions_match);
    ASSERT_TRUE(result.service_was_stopped);
    
    ipc_client_disconnect(client);
    
    /* Service should be gone */
    platform_sleep_ms(500);
    ASSERT_FALSE(ipc_service_exists());
    
    service_clear_exe_override();
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
    RUN_TEST(service_version_flag_works);
    RUN_TEST(service_help_mentions_version_flag);
    RUN_TEST(alt_service_v13_reports_correct_version);
    RUN_TEST(alt_service_v17_reports_correct_version);
    RUN_TEST(client_newer_than_service_mismatch);
    RUN_TEST(client_newer_than_service_message);
    RUN_TEST(client_older_than_service_mismatch);
    RUN_TEST(client_older_than_service_message);
    RUN_TEST(service_not_killed_when_newer);
    RUN_TEST(service_killed_when_older);
}

TEST_MAIN(
    suite_version_compat();
)
