/*
 * ipc_ping_test.c -- Test basic service liveness via PING requests
 *
 * Usage: ipc_ping_test [--once|--continuous] [--interval <ms>] [--count <n>] [--timeout <ms>]
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
    printf("  --once              Send a single ping (default)\n");
    printf("  --continuous        Continuously ping until interrupted\n");
    printf("  --interval <ms>     Interval between pings in milliseconds (default: 1000)\n");
    printf("  --count <n>         Send N pings and show statistics\n");
    printf("  --timeout <ms>      Connection timeout in milliseconds (default: 5000)\n");
    printf("  --verbose, -v       Show verbose output\n");
    printf("  --help, -h          Show this help\n");
    print_help_footer();
}

int main(int argc, char **argv) {
    IpcPingOptions opts;
    
    if (!parse_ping_args(argc, argv, &opts)) {
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
    
    /* Check if service is running */
    if (!check_service_running()) {
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    print_test_header("Ping");
    
    if (opts.verbose) {
        printf("Mode:       %s\n", opts.continuous ? "continuous" : (opts.count > 0 ? "batch" : "once"));
        if (opts.count > 0) printf("Count:      %d\n", opts.count);
        if (opts.continuous || opts.count > 0) printf("Interval:   %d ms\n", opts.interval_ms);
        printf("Timeout:    %d ms\n", opts.timeout_ms);
        printf("\n");
    }
    
    /* Statistics for batch mode */
    int success_count = 0;
    int fail_count = 0;
    double min_time = 999999.0;
    double max_time = 0.0;
    double total_time = 0.0;
    
    int ping_num = 0;
    int max_pings = opts.count > 0 ? opts.count : (opts.continuous ? 999999999 : 1);
    
    for (ping_num = 1; ping_num <= max_pings; ping_num++) {
        /* Connect to service */
        NcdIpcClient *client = ipc_connect_with_timeout(opts.timeout_ms);
        if (!client) {
            printf("Ping %d: FAILED (connection error)\n", ping_num);
            fail_count++;
            if (!opts.continuous && opts.count == 0) {
                ipc_client_cleanup();
                return IPC_EXIT_NOT_RUNNING;
            }
            if (ping_num < max_pings) {
#if NCD_PLATFORM_WINDOWS
                Sleep(opts.interval_ms);
#else
                usleep(opts.interval_ms * 1000);
#endif
            }
            continue;
        }
        
        /* Send ping and measure time */
        ipc_time_t start, end;
        ipc_get_time(&start);
        
        NcdIpcResult result = ipc_client_ping(client);
        
        ipc_get_time(&end);
        double elapsed_ms = ipc_elapsed_ms(&start, &end);
        
        ipc_client_disconnect(client);
        
        if (result == NCD_IPC_OK) {
            success_count++;
            total_time += elapsed_ms;
            if (elapsed_ms < min_time) min_time = elapsed_ms;
            if (elapsed_ms > max_time) max_time = elapsed_ms;
            
            if (opts.count > 0 || opts.verbose) {
                printf("Ping %d: %.2f ms\n", ping_num, elapsed_ms);
            }
        } else {
            fail_count++;
            printf("Ping %d: FAILED (%s)\n", ping_num, ipc_error_string(result));
        }
        
        /* Sleep between pings if more to do */
        if (ping_num < max_pings) {
#if NCD_PLATFORM_WINDOWS
            Sleep(opts.interval_ms);
#else
            usleep(opts.interval_ms * 1000);
#endif
        }
    }
    
    /* Print summary */
    printf("\n");
    if (opts.count > 0 || opts.continuous) {
        printf("-------------------------\n");
        printf("Pings:      %d\n", success_count + fail_count);
        printf("Successful: %d\n", success_count);
        printf("Failed:     %d\n", fail_count);
        if (success_count > 0) {
            printf("Min:        %.2f ms\n", min_time);
            printf("Max:        %.2f ms\n", max_time);
            printf("Avg:        %.2f ms\n", total_time / success_count);
        }
    }
    
    if (fail_count == 0) {
        printf("Result:     PASS\n");
    } else if (success_count == 0) {
        printf("Result:     FAIL\n");
    } else {
        printf("Result:     PARTIAL\n");
    }
    
    ipc_client_cleanup();
    
    if (fail_count == 0) return IPC_EXIT_SUCCESS;
    if (success_count == 0) return IPC_EXIT_NOT_RUNNING;
    return IPC_EXIT_ERROR;
}
