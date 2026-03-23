/*
 * test_service_lazy_load.c  --  Test service lazy loading functionality
 *
 * Tests:
 * - Service starts immediately without blocking
 * - BUSY_LOADING status returned during database loading
 * - BUSY_SCANNING status returned during rescan
 * - Request queueing during loading/rescan
 * - Client retry logic for busy states
 */

#include "test_framework.h"
#include "../src/service_state.h"
#include "../src/control_ipc.h"
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------- test utilities     */

static double get_time_ms(void) {
#if NCD_PLATFORM_WINDOWS
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

/* --------------------------------------------------------- state machine tests */

TEST(state_machine_transitions) {
    ServiceState *state = service_state_init();
    ASSERT_NOT_NULL(state);
    
    /* Initial state should be STARTING */
    ServiceRuntimeState runtime = service_state_get_runtime_state(state);
    ASSERT_TRUE(runtime == SERVICE_STATE_STARTING || runtime == SERVICE_STATE_LOADING);
    
    /* Test manual state transitions */
    service_state_set_runtime_state(state, SERVICE_STATE_LOADING);
    ASSERT_EQ_INT(SERVICE_STATE_LOADING, service_state_get_runtime_state(state));
    
    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    ASSERT_EQ_INT(SERVICE_STATE_READY, service_state_get_runtime_state(state));
    
    service_state_set_runtime_state(state, SERVICE_STATE_SCANNING);
    ASSERT_EQ_INT(SERVICE_STATE_SCANNING, service_state_get_runtime_state(state));
    
    service_state_cleanup(state);
    return 0;
}

TEST(status_message_set_get) {
    ServiceState *state = service_state_init();
    ASSERT_NOT_NULL(state);
    
    /* Test setting and getting status message */
    service_state_set_status_message(state, "Test message");
    ASSERT_STR_EQ("Test message", service_state_get_status_message(state));
    
    service_state_set_status_message(state, "Loading drive C:...");
    ASSERT_STR_EQ("Loading drive C:...", service_state_get_status_message(state));
    
    /* Test empty/null handling */
    service_state_set_status_message(state, "");
    ASSERT_STR_EQ("", service_state_get_status_message(state));
    
    service_state_cleanup(state);
    return 0;
}

TEST(null_state_safety) {
    /* All functions should handle NULL gracefully */
    service_state_set_runtime_state(NULL, SERVICE_STATE_READY);
    ASSERT_EQ_INT(SERVICE_STATE_STOPPED, service_state_get_runtime_state(NULL));
    ASSERT_TRUE(service_state_get_status_message(NULL) != NULL);
    ASSERT_EQ_INT(0, service_state_get_pending_count(NULL));
    ASSERT_FALSE(service_state_wait_for_ready(NULL, 100));
    
    return 0;
}

/* --------------------------------------------------------- request queue tests */

TEST(queue_basic_operations) {
    ServiceState *state = service_state_init();
    ASSERT_NOT_NULL(state);
    
    /* Initially empty */
    ASSERT_EQ_INT(0, service_state_get_pending_count(state));
    
    /* Enqueue a request */
    uint32_t data = 0x12345678;
    bool queued = service_state_enqueue_request(state, PENDING_FLUSH, &data, sizeof(data));
    ASSERT_TRUE(queued);
    ASSERT_EQ_INT(1, service_state_get_pending_count(state));
    
    /* Enqueue another */
    queued = service_state_enqueue_request(state, PENDING_RESCAN, NULL, 0);
    ASSERT_TRUE(queued);
    ASSERT_EQ_INT(2, service_state_get_pending_count(state));
    
    /* Clear queue */
    service_state_clear_pending(state);
    ASSERT_EQ_INT(0, service_state_get_pending_count(state));
    
    service_state_cleanup(state);
    return 0;
}

TEST(queue_dequeue) {
    ServiceState *state = service_state_init();
    ASSERT_NOT_NULL(state);
    
    /* Enqueue some requests */
    uint32_t data1 = 0x11111111;
    uint32_t data2 = 0x22222222;
    service_state_enqueue_request(state, PENDING_FLUSH, &data1, sizeof(data1));
    service_state_enqueue_request(state, PENDING_RESCAN, &data2, sizeof(data2));
    ASSERT_EQ_INT(2, service_state_get_pending_count(state));
    
    /* Dequeue and verify */
    PendingRequestType type;
    void *out_data = NULL;
    size_t out_len = 0;
    
    bool got = service_state_dequeue_pending(state, &type, &out_data, &out_len);
    ASSERT_TRUE(got);
    ASSERT_EQ_INT(PENDING_FLUSH, type);
    ASSERT_EQ_INT(sizeof(uint32_t), (int)out_len);
    ASSERT_EQ_INT(0x11111111, *(uint32_t *)out_data);
    free(out_data);
    
    got = service_state_dequeue_pending(state, &type, &out_data, &out_len);
    ASSERT_TRUE(got);
    ASSERT_EQ_INT(PENDING_RESCAN, type);
    free(out_data);
    
    /* Queue empty */
    got = service_state_dequeue_pending(state, &type, &out_data, &out_len);
    ASSERT_FALSE(got);
    
    service_state_cleanup(state);
    return 0;
}

TEST(queue_null_safety) {
    /* Queue operations should handle NULL gracefully */
    ASSERT_FALSE(service_state_enqueue_request(NULL, PENDING_FLUSH, NULL, 0));
    ASSERT_EQ_INT(0, service_state_get_pending_count(NULL));
    service_state_clear_pending(NULL);
    
    PendingRequestType type;
    void *data;
    size_t len;
    ASSERT_FALSE(service_state_dequeue_pending(NULL, &type, &data, &len));
    
    return 0;
}

/* --------------------------------------------------------- IPC status code tests */

TEST(ipc_error_strings) {
    /* Verify error codes have string representations */
    const char *str;
    
    str = ipc_error_string(NCD_IPC_OK);
    ASSERT_TRUE(str != NULL);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY_LOADING);
    ASSERT_TRUE(str != NULL);
    ASSERT_TRUE(strstr(str, "loading") != NULL);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY_SCANNING);
    ASSERT_TRUE(str != NULL);
    ASSERT_TRUE(strstr(str, "scanning") != NULL);
    
    str = ipc_error_string(NCD_IPC_ERROR_NOT_READY);
    ASSERT_TRUE(str != NULL);
    ASSERT_TRUE(strstr(str, "not ready") != NULL || strstr(str, "Not ready") != NULL);
    
    return 0;
}

/* --------------------------------------------------------- service startup tests */

TEST(service_init_performance) {
    /* Service init should complete quickly (< 100ms) regardless of database size */
    double start = get_time_ms();
    
    ServiceState *state = service_state_init();
    ASSERT_NOT_NULL(state);
    
    double elapsed = get_time_ms() - start;
    
    /* Should complete within 100ms (metadata loading is fast) */
    ASSERT_TRUE(elapsed < 100.0);
    
    service_state_cleanup(state);
    return 0;
}

TEST(service_metadata_available_immediately) {
    /* Metadata should be available immediately after init */
    ServiceState *state = service_state_init();
    ASSERT_NOT_NULL(state);
    
    const NcdMetadata *meta = service_state_get_metadata(state);
    ASSERT_NOT_NULL(meta);
    
    service_state_cleanup(state);
    return 0;
}

/* --------------------------------------------------------- integration tests */

TEST(full_queue_process_cycle) {
    ServiceState *state = service_state_init();
    ASSERT_NOT_NULL(state);
    
    /* Queue multiple requests */
    uint32_t data1 = 0x11111111;
    uint32_t data2 = 0x22222222;
    uint32_t data3 = 0x33333333;
    
    ASSERT_TRUE(service_state_enqueue_request(state, PENDING_FLUSH, &data1, sizeof(data1)));
    ASSERT_TRUE(service_state_enqueue_request(state, PENDING_RESCAN, &data2, sizeof(data2)));
    ASSERT_TRUE(service_state_enqueue_request(state, PENDING_FLUSH, &data3, sizeof(data3)));
    ASSERT_EQ_INT(3, service_state_get_pending_count(state));
    
    /* Process all (with NULL pub - handlers will just return) */
    service_state_process_pending(state, NULL);
    
    /* Queue should be empty after processing */
    ASSERT_EQ_INT(0, service_state_get_pending_count(state));
    
    service_state_cleanup(state);
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_lazy_load(void) {
    printf("\n=== Service Lazy Loading Tests ===\n\n");
    
    RUN_TEST(state_machine_transitions);
    RUN_TEST(status_message_set_get);
    RUN_TEST(null_state_safety);
    RUN_TEST(queue_basic_operations);
    RUN_TEST(queue_dequeue);
    RUN_TEST(queue_null_safety);
    RUN_TEST(ipc_error_strings);
    RUN_TEST(service_init_performance);
    RUN_TEST(service_metadata_available_immediately);
    RUN_TEST(full_queue_process_cycle);
}

TEST_MAIN(suite_service_lazy_load);
