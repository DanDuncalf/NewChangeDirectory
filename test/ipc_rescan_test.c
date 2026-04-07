/*
 * ipc_rescan_test.c -- Test database rescan requests
 *
 * Usage: ipc_rescan_test [--full] [--drive <letter>] [--drives <letters>] [--path <path>] [--wait] [--timeout <sec>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_test_common.h"
#include "../src/control_ipc.h"

#if !NCD_PLATFORM_WINDOWS
#include <unistd.h>
#include <sys/select.h>
#else
#include <windows.h>
#endif

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --full              Request full rescan (default)\n");
    printf("  --drive <letter>    Scan specific drive (e.g., --drive C)\n");
    printf("  --drives <letters>  Scan multiple drives (e.g., --drives C,D,E)\n");
    printf("  --path <path>       Scan specific subdirectory\n");
    printf("  --wait, -w          Wait for rescan to complete\n");
    printf("  --status, -s        Check current scan status\n");
    printf("  --timeout <sec>     Timeout for --wait (default: 300)\n");
    printf("  --verbose, -v       Show verbose output\n");
    printf("  --help, -h          Show this help\n");
    print_help_footer();
}

static int check_status(void) {
    NcdIpcClient *client = ipc_connect_with_timeout(5000);
    if (!client) {
        printf("Service: NOT RUNNING\n");
        return IPC_EXIT_NOT_RUNNING;
    }
    
    NcdIpcStateInfo info;
    NcdIpcResult result = ipc_client_get_state_info(client, &info);
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        printf("Error: %s\n", ipc_error_string(result));
        return ipc_result_to_exit_code(result);
    }
    
    /* Check if generations are changing (indicates scanning) */
    uint64_t initial_meta_gen = info.meta_generation;
    uint64_t initial_db_gen = info.db_generation;
    
    printf("Current state:\n");
    printf("  Metadata generation: %llu\n", (unsigned long long)initial_meta_gen);
    printf("  Database generation: %llu\n", (unsigned long long)initial_db_gen);
    
    /* Wait a bit and check again */
#if NCD_PLATFORM_WINDOWS
    Sleep(500);
#else
    usleep(500000);
#endif
    
    client = ipc_client_connect();
    if (!client) {
        printf("Service disconnected during status check\n");
        return IPC_EXIT_NOT_RUNNING;
    }
    
    result = ipc_client_get_state_info(client, &info);
    ipc_client_disconnect(client);
    
    if (result == NCD_IPC_OK) {
        if (info.db_generation != initial_db_gen) {
            printf("\nStatus: SCANNING (database generation changed)\n");
        } else {
            printf("\nStatus: IDLE\n");
        }
    }
    
    return IPC_EXIT_SUCCESS;
}

static bool wait_for_rescan_complete(int timeout_sec) {
    printf("Waiting for rescan to complete (timeout: %d seconds)...\n", timeout_sec);
    
    uint64_t prev_db_gen = 0;
    int stable_count = 0;
    int elapsed_sec = 0;
    
    while (elapsed_sec < timeout_sec) {
        NcdIpcClient *client = ipc_connect_with_timeout(5000);
        if (!client) {
            printf("  [ERROR] Service disconnected\n");
            return false;
        }
        
        NcdIpcStateInfo info;
        NcdIpcResult result = ipc_client_get_state_info(client, &info);
        ipc_client_disconnect(client);
        
        if (result != NCD_IPC_OK) {
            printf("  [ERROR] %s\n", ipc_error_string(result));
            return false;
        }
        
        /* Check if database generation is stable */
        if (info.db_generation == prev_db_gen && prev_db_gen != 0) {
            stable_count++;
            if (stable_count >= 3) {
                printf("  [COMPLETE] Rescan finished (DB generation stable)\n");
                return true;
            }
        } else {
            stable_count = 0;
            prev_db_gen = info.db_generation;
            printf("  [WAITING] DB generation: %llu (checking...)\n",
                   (unsigned long long)prev_db_gen);
        }
        
        /* Wait 1 second */
#if NCD_PLATFORM_WINDOWS
        Sleep(1000);
#else
        sleep(1);
#endif
        elapsed_sec++;
    }
    
    printf("  [TIMEOUT] Rescan did not complete within %d seconds\n", timeout_sec);
    return false;
}

int main(int argc, char **argv) {
    IpcRescanOptions opts;
    
    if (!parse_rescan_args(argc, argv, &opts)) {
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
    
    print_test_header("Rescan");
    
    /* Check if service is running */
    if (!check_service_running()) {
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    printf("Service: RUNNING\n\n");
    
    /* Handle status check */
    if (opts.status) {
        int result = check_status();
        ipc_client_cleanup();
        return result;
    }
    
    /* Build drive mask */
    bool drive_mask[26] = {false};
    
    if (opts.drives[0]) {
        /* Parse comma-separated drive letters */
        for (size_t i = 0; i < strlen(opts.drives); i++) {
            char c = opts.drives[i];
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            if (c >= 'A' && c <= 'Z') {
                drive_mask[c - 'A'] = true;
            }
        }
    } else if (opts.full) {
        /* Full rescan - all drives */
        for (int i = 0; i < 26; i++) {
            drive_mask[i] = true;
        }
    }
    
    if (opts.verbose) {
        printf("Request details:\n");
        if (opts.path[0]) {
            printf("  Type:     Subdirectory scan\n");
            printf("  Path:     %s\n", opts.path);
        } else if (opts.drives[0] || opts.full) {
            printf("  Type:     Drive scan\n");
            printf("  Drives:   ");
            int count = 0;
            for (int i = 0; i < 26; i++) {
                if (drive_mask[i]) {
                    printf("%c ", 'A' + i);
                    count++;
                }
            }
            if (count == 0) printf("(none selected)");
            printf("\n");
        }
        printf("  Wait:     %s\n", opts.wait ? "yes" : "no");
        printf("  Timeout:  %d seconds\n\n", opts.timeout_sec);
    }
    
    /* Connect to service */
    NcdIpcClient *client = ipc_connect_with_timeout(5000);
    if (!client) {
        printf("Error: Failed to connect to service\n");
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    /* Send rescan request */
    ipc_time_t start, end;
    ipc_get_time(&start);
    
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    
    ipc_get_time(&end);
    double elapsed_ms = ipc_elapsed_ms(&start, &end);
    
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        if (result == NCD_IPC_ERROR_BUSY || result == NCD_IPC_ERROR_BUSY_SCANNING) {
            printf("Result: BUSY (scan already in progress)\n");
            printf("Time:   %.2f ms\n", elapsed_ms);
            printf("Use --status to check progress.\n");
            ipc_client_cleanup();
            return IPC_EXIT_BUSY;
        }
        
        printf("Result: FAILED (%s)\n", ipc_error_string(result));
        printf("Time:   %.2f ms\n", elapsed_ms);
        ipc_client_cleanup();
        return ipc_result_to_exit_code(result);
    }
    
    printf("Result: ACCEPTED\n");
    printf("Time:   %.2f ms\n", elapsed_ms);
    
    /* Wait for completion if requested */
    if (opts.wait) {
        printf("\n");
        bool completed = wait_for_rescan_complete(opts.timeout_sec);
        ipc_client_cleanup();
        return completed ? IPC_EXIT_SUCCESS : IPC_EXIT_TIMEOUT;
    }
    
    ipc_client_cleanup();
    return IPC_EXIT_SUCCESS;
}
