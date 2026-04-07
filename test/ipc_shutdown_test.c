/*
 * ipc_shutdown_test.c -- Test graceful shutdown requests
 *
 * Usage: ipc_shutdown_test [--timeout <sec>] [--force] [--verify]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_test_common.h"
#include "../src/control_ipc.h"

#if !NCD_PLATFORM_WINDOWS
#include <unistd.h>
#endif

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --timeout <sec>     Timeout for shutdown confirmation (default: 30)\n");
    printf("  --force, -f         Force shutdown if graceful fails\n");
    printf("  --verify            Verify service stopped after request\n");
    printf("  --verbose, -v       Show verbose output\n");
    printf("  --help, -h          Show this help\n");
    print_help_footer();
}

static bool wait_for_service_stop(int timeout_sec) {
    printf("Waiting for service to stop...\n");
    
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (!ipc_service_exists()) {
            return true;
        }
#if NCD_PLATFORM_WINDOWS
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    
    return false;
}

int main(int argc, char **argv) {
    IpcShutdownOptions opts;
    
    if (!parse_shutdown_args(argc, argv, &opts)) {
        if (opts.help) {
            print_usage(argv[0]);
            return IPC_EXIT_SUCCESS;
        }
        return IPC_EXIT_INVALID;
    }
    
    /* Initialize IPC client */
    if (ipc_client_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize IPC client\n");
        return IPC_EXIT_ERROR;
    }
    
    print_test_header("Shutdown");
    
    if (opts.verbose) {
        printf("Timeout:    %d seconds\n", opts.timeout_sec);
        printf("Force:      %s\n", opts.force ? "yes" : "no");
        printf("Verify:     %s\n\n", opts.verify ? "yes" : "no");
    }
    
    /* Check if service is running */
    if (!ipc_service_exists()) {
        printf("Service: NOT RUNNING\n");
        printf("Nothing to shutdown.\n");
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    printf("Service: RUNNING\n");
    printf("Requesting graceful shutdown...\n\n");
    
    /* Connect to service */
    NcdIpcClient *client = ipc_connect_with_timeout(5000);
    if (!client) {
        printf("Error: Failed to connect to service\n");
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    /* Request shutdown */
    ipc_time_t start, end;
    ipc_get_time(&start);
    
    NcdIpcResult result = ipc_client_request_shutdown(client);
    
    ipc_get_time(&end);
    double elapsed_ms = ipc_elapsed_ms(&start, &end);
    
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        printf("Shutdown request: FAILED (%s)\n", ipc_error_string(result));
        printf("Time: %.2f ms\n", elapsed_ms);
        
        if (opts.force && result == NCD_IPC_ERROR_BUSY) {
            printf("\nForce flag set, but service is busy.\n");
            printf("Note: Force shutdown is not implemented in this version.\n");
        }
        
        ipc_client_cleanup();
        return ipc_result_to_exit_code(result);
    }
    
    printf("Shutdown request: ACCEPTED\n");
    printf("Time: %.2f ms\n", elapsed_ms);
    
    /* Verify shutdown if requested */
    if (opts.verify) {
        printf("\n");
        bool stopped = wait_for_service_stop(opts.timeout_sec);
        
        if (stopped) {
            printf("Service: STOPPED\n");
            printf("Result: PASS\n");
            ipc_client_cleanup();
            return IPC_EXIT_SUCCESS;
        } else {
            printf("Service: STILL RUNNING (timeout)\n");
            printf("Result: TIMEOUT\n");
            ipc_client_cleanup();
            return IPC_EXIT_TIMEOUT;
        }
    } else {
        printf("\nShutdown request sent successfully.\n");
        printf("Use --verify to wait for confirmation.\n");
    }
    
    ipc_client_cleanup();
    return IPC_EXIT_SUCCESS;
}
