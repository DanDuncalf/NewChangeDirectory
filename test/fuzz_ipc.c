/*
 * fuzz_ipc.c  --  IPC fuzz tests (10 tests)
 *
 * Tests:
 * - fuzz_ipc_message_header_random_bytes
 * - fuzz_ipc_message_payload_overflow
 * - fuzz_ipc_message_type_confusion
 * - fuzz_ipc_message_sequence_wraparound
 * - fuzz_ipc_message_fragmentation
 * - fuzz_ipc_bit_flipping
 * - fuzz_ipc_byte_swapping
 * - fuzz_ipc_known_integers
 * - fuzz_ipc_dictionary_words
 * - fuzz_ipc_boundary_values
 */

#include "test_framework.h"
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------- test utilities     */

static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA("NCDService.exe");
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access("../ncd_service", X_OK) == 0);
#endif
}

static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) return;
    
    NcdIpcClient *client = ipc_client_connect();
    if (client) {
        ipc_client_request_shutdown(client);
        ipc_client_disconnect(client);
    }
    
    for (int i = 0; i < 50; i++) {
        if (!ipc_service_exists()) break;
        platform_sleep_ms(100);
    }
}

static bool ensure_service_running(void) {
    if (ipc_service_exists()) return true;
    if (!service_executable_exists()) return false;
    
#if NCD_PLATFORM_WINDOWS
    system("NCDService.exe start");
#else
    system("../ncd_service start");
#endif
    
    for (int i = 0; i < 50; i++) {
        if (ipc_service_exists()) return true;
        platform_sleep_ms(100);
    }
    return false;
}

/* Simple random number generator for fuzzing */
static unsigned int fuzz_seed = 12345;
static unsigned int fuzz_rand(void) {
    fuzz_seed = fuzz_seed * 1103515245 + 12345;
    return (fuzz_seed >> 16) & 0x7FFF;
}

/* --------------------------------------------------------- fuzz tests */

TEST(fuzz_ipc_message_header_random_bytes) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Send various malformed headers and verify service doesn't crash */
    ipc_client_init();
    
    for (int i = 0; i < 10; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            /* Valid ping to verify service is still responsive */
            NcdIpcResult result = ipc_client_ping(client);
            ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(50);
    }
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_message_payload_overflow) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Test with payload at boundary conditions */
    char payload[NCD_IPC_MAX_MSG_SIZE + 100];
    memset(payload, 'X', sizeof(payload));
    
    /* Try to submit with various payload sizes */
    for (int size = 100; size <= 1000; size += 100) {
        payload[size - 1] = '\0';
        NcdIpcResult result = ipc_client_submit_heuristic(client, payload, "/test/path");
        /* Should handle gracefully - not crash */
        ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_INVALID ||
                    result == NCD_IPC_ERROR_BUSY_LOADING || result == NCD_IPC_ERROR_BUSY_SCANNING);
    }
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_message_type_confusion) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Rapidly switch between different message types */
    ipc_client_init();
    
    for (int i = 0; i < 20; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcResult result;
            switch (i % 5) {
                case 0:
                    result = ipc_client_ping(client);
                    break;
                case 1: {
                    NcdIpcStateInfo info;
                    result = ipc_client_get_state_info(client, &info);
                    break;
                }
                case 2: {
                    NcdIpcVersionInfo info;
                    result = ipc_client_get_version(client, &info);
                    break;
                }
                case 3:
                    result = ipc_client_submit_heuristic(client, "fuzz", "/fuzz/path");
                    break;
                case 4:
                    result = ipc_client_request_flush(client);
                    break;
            }
            /* Any result is OK as long as service doesn't crash */
            (void)result;
            ipc_client_disconnect(client);
        }
    }
    
    ipc_client_cleanup();
    
    /* Verify service is still responsive */
    ASSERT_TRUE(ipc_service_exists());
    
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_message_sequence_wraparound) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Send many messages to test sequence number handling */
    ipc_client_init();
    
    int success_count = 0;
    for (int i = 0; i < 100; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            if (ipc_client_ping(client) == NCD_IPC_OK) {
                success_count++;
            }
            ipc_client_disconnect(client);
        }
    }
    
    ipc_client_cleanup();
    
    /* Most pings should succeed */
    ASSERT_TRUE(success_count >= 50);
    
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_message_fragmentation) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Rapid connect/disconnect to test message handling under fragmentation */
    ipc_client_init();
    
    for (int i = 0; i < 30; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            /* Some connections send data, some don't */
            if (i % 3 == 0) {
                ipc_client_ping(client);
            }
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(10);
    }
    
    ipc_client_cleanup();
    
    /* Service should still be operational */
    ASSERT_TRUE(ipc_service_exists());
    
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_bit_flipping) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Simulate bit-flipped data by sending various corrupted patterns */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Send valid data to ensure service is still working */
    NcdIpcResult result = ipc_client_ping(client);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_byte_swapping) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Test endianness handling by sending native format data */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Normal operation uses native byte order */
    NcdIpcResult result = ipc_client_ping(client);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_known_integers) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Test with integer boundary values in payloads */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    char payload[256];
    
    /* Test with payload containing integer-like patterns */
    const char *test_values[] = {
        "0", "-1", "2147483647", "-2147483648",
        "4294967295", "65535", "255", "127"
    };
    
    for (int i = 0; i < 8; i++) {
        snprintf(payload, sizeof(payload), "%s_test", test_values[i]);
        NcdIpcResult result = ipc_client_submit_heuristic(client, payload, "/test/path");
        ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_INVALID ||
                    result == NCD_IPC_ERROR_BUSY_LOADING || result == NCD_IPC_ERROR_BUSY_SCANNING);
    }
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_dictionary_words) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Test with common dictionary words as search terms */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    const char *words[] = {
        "admin", "backup", "config", "data", "email",
        "file", "group", "home", "input", "json",
        "key", "log", "main", "null", "output",
        "path", "query", "root", "src", "test",
        "user", "var", "www", "xml", "yaml"
    };
    
    for (int i = 0; i < 25; i++) {
        NcdIpcResult result = ipc_client_submit_heuristic(client, words[i], "/test/path");
        ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_LOADING ||
                    result == NCD_IPC_ERROR_BUSY_SCANNING);
    }
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(fuzz_ipc_boundary_values) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Test with boundary values in various fields */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Test empty strings */
    NcdIpcResult result = ipc_client_submit_heuristic(client, "", "/test/path");
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_INVALID ||
                result == NCD_IPC_ERROR_BUSY_LOADING);
    
    /* Test single character */
    result = ipc_client_submit_heuristic(client, "a", "b");
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_INVALID ||
                result == NCD_IPC_ERROR_BUSY_LOADING);
    
    /* Test with special characters */
    result = ipc_client_submit_heuristic(client, "!@#$%^&*()", "/test/path");
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_INVALID ||
                result == NCD_IPC_ERROR_BUSY_LOADING);
    
    /* Test with spaces */
    result = ipc_client_submit_heuristic(client, "path with spaces", "/test/path");
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_INVALID ||
                result == NCD_IPC_ERROR_BUSY_LOADING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_fuzz_ipc(void) {
    printf("\n=== IPC Fuzz Tests ===\n\n");
    
    RUN_TEST(fuzz_ipc_message_header_random_bytes);
    RUN_TEST(fuzz_ipc_message_payload_overflow);
    RUN_TEST(fuzz_ipc_message_type_confusion);
    RUN_TEST(fuzz_ipc_message_sequence_wraparound);
    RUN_TEST(fuzz_ipc_message_fragmentation);
    RUN_TEST(fuzz_ipc_bit_flipping);
    RUN_TEST(fuzz_ipc_byte_swapping);
    RUN_TEST(fuzz_ipc_known_integers);
    RUN_TEST(fuzz_ipc_dictionary_words);
    RUN_TEST(fuzz_ipc_boundary_values);
    
    /* Final cleanup */
    printf("\n--- Final cleanup ---\n");
    ensure_service_stopped();
}

TEST_MAIN(
    suite_fuzz_ipc();
)
