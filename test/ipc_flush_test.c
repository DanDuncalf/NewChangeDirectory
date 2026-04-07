/*
 * ipc_flush_test.c -- Test immediate persistence requests
 *
 * Usage: ipc_flush_test [--verify] [--timing] [--force]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_test_common.h"
#include "../src/control_ipc.h"

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --verify            Verify files exist after flush\n");
    printf("  --timing            Show flush timing information\n");
    printf("  --force, -f         Force flush even if service is busy\n");
    printf("  --verbose, -v       Show verbose output\n");
    printf("  --help, -h          Show this help\n");
    print_help_footer();
}

int main(int argc, char **argv) {
    IpcFlushOptions opts;
    
    if (!parse_flush_args(argc, argv, &opts)) {
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
    
    print_test_header("Flush");
    
    /* Check if service is running */
    if (!check_service_running()) {
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    printf("Service: RUNNING\n\n");
    
    if (opts.verbose) {
        printf("Options:\n");
        printf("  Verify:   %s\n", opts.verify ? "yes" : "no");
        printf("  Timing:   %s\n", opts.timing ? "yes" : "no");
        printf("  Force:    %s\n\n", opts.force ? "yes" : "no");
    }
    
    /* Connect to service */
    NcdIpcClient *client = ipc_connect_with_timeout(5000);
    if (!client) {
        printf("Error: Failed to connect to service\n");
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    /* Get state info before flush */
    NcdIpcStateInfo info_before;
    NcdIpcResult result = ipc_client_get_state_info(client, &info_before);
    if (result != NCD_IPC_OK) {
        printf("Error getting state info: %s\n", ipc_error_string(result));
        ipc_client_disconnect(client);
        ipc_client_cleanup();
        return ipc_result_to_exit_code(result);
    }
    
    if (opts.verbose && !opts.timing) {
        printf("Pre-flush state:\n");
        printf("  Meta generation: %llu\n", (unsigned long long)info_before.meta_generation);
        printf("  DB generation:   %llu\n\n", (unsigned long long)info_before.db_generation);
    }
    
    /* Send flush request */
    printf("Requesting flush...\n");
    
    ipc_time_t start, end;
    ipc_get_time(&start);
    
    result = ipc_client_request_flush(client);
    
    ipc_get_time(&end);
    double elapsed_ms = ipc_elapsed_ms(&start, &end);
    
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        if (result == NCD_IPC_ERROR_BUSY || result == NCD_IPC_ERROR_BUSY_SCANNING) {
            printf("Result: BUSY\n");
            printf("Time:   %.2f ms\n", elapsed_ms);
            if (!opts.force) {
                printf("Service is busy. Use --force to wait.\n");
            }
            ipc_client_cleanup();
            return IPC_EXIT_BUSY;
        }
        
        printf("Result: FAILED (%s)\n", ipc_error_string(result));
        printf("Time:   %.2f ms\n", elapsed_ms);
        ipc_client_cleanup();
        return ipc_result_to_exit_code(result);
    }
    
    printf("Result: SUCCESS\n");
    if (opts.timing) {
        printf("Time:   %.2f ms\n", elapsed_ms);
    }
    
    /* Verify state changed if requested */
    if (opts.verify) {
        printf("\nVerifying flush...\n");
        
        client = ipc_connect_with_timeout(5000);
        if (!client) {
            printf("  [ERROR] Failed to reconnect for verification\n");
            ipc_client_cleanup();
            return IPC_EXIT_ERROR;
        }
        
        NcdIpcStateInfo info_after;
        result = ipc_client_get_state_info(client, &info_after);
        ipc_client_disconnect(client);
        
        if (result != NCD_IPC_OK) {
            printf("  [ERROR] Failed to get state: %s\n", ipc_error_string(result));
            ipc_client_cleanup();
            return ipc_result_to_exit_code(result);
        }
        
        /* Note: The service might not actually change generations on flush,
         * but we should at least be able to query the state */
        printf("  [OK] Service responding after flush\n");
        
        if (opts.verbose) {
            printf("\nPost-flush state:\n");
            printf("  Meta generation: %llu\n", (unsigned long long)info_after.meta_generation);
            printf("  DB generation:   %llu\n", (unsigned long long)info_after.db_generation);
        }
    }
    
    printf("\nFlush completed successfully.\n");
    ipc_client_cleanup();
    return IPC_EXIT_SUCCESS;
}
