/*
 * ipc_state_test.c -- Test state information retrieval
 *
 * Usage: ipc_state_test [--info|--version|--all] [--watch] [--interval <ms>] [--json]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_test_common.h"
#include "../src/control_ipc.h"

#if !NCD_PLATFORM_WINDOWS
#include <unistd.h>
#include <signal.h>

static volatile int g_watch_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_watch_running = 0;
}
#endif

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --info              Show state information\n");
    printf("  --version, -V       Show version information\n");
    printf("  --all, -a           Show all information (default)\n");
    printf("  --watch, -w         Watch mode - poll for changes\n");
    printf("  --interval <ms>     Polling interval in milliseconds (default: 1000, min: 100)\n");
    printf("  --json, -j          Output as JSON\n");
    printf("  --verbose, -v       Show verbose output\n");
    printf("  --help, -h          Show this help\n");
    print_help_footer();
}

static int show_info(bool json_output, bool verbose) {
    NcdIpcClient *client = ipc_connect_with_timeout(5000);
    if (!client) {
        if (!json_output) {
            printf("Service: NOT RUNNING\n");
        } else {
            printf("{\"error\": \"Service not running\"}\n");
        }
        return IPC_EXIT_NOT_RUNNING;
    }
    
    NcdIpcStateInfo info;
    NcdIpcResult result = ipc_client_get_state_info(client, &info);
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        if (!json_output) {
            printf("Error: %s\n", ipc_error_string(result));
        } else {
            printf("{\"error\": \"%s\"}\n", ipc_error_string(result));
        }
        return ipc_result_to_exit_code(result);
    }
    
    if (verbose && !json_output) {
        printf("Service: RUNNING\n\n");
    }
    
    if (json_output) {
        print_json_state_info(&info);
    } else {
        print_human_state_info(&info);
    }
    
    return IPC_EXIT_SUCCESS;
}

static int show_version(bool json_output, bool verbose) {
    NcdIpcClient *client = ipc_connect_with_timeout(5000);
    if (!client) {
        if (!json_output) {
            printf("Service: NOT RUNNING\n");
        } else {
            printf("{\"error\": \"Service not running\"}\n");
        }
        return IPC_EXIT_NOT_RUNNING;
    }
    
    NcdIpcVersionInfo info;
    NcdIpcResult result = ipc_client_get_version(client, &info);
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        if (!json_output) {
            printf("Error: %s\n", ipc_error_string(result));
        } else {
            printf("{\"error\": \"%s\"}\n", ipc_error_string(result));
        }
        return ipc_result_to_exit_code(result);
    }
    
    if (verbose && !json_output) {
        printf("Service: RUNNING\n\n");
    }
    
    if (json_output) {
        print_json_version_info(&info);
    } else {
        print_human_version_info(&info);
    }
    
    return IPC_EXIT_SUCCESS;
}

static int watch_mode(const IpcStateOptions *opts) {
#if !NCD_PLATFORM_WINDOWS
    signal(SIGINT, sigint_handler);
#endif
    
    printf("Watch mode enabled. Press Ctrl+C to stop.\n");
    printf("Polling every %d ms\n\n", opts->interval_ms);
    
    uint64_t last_meta_gen = 0;
    uint64_t last_db_gen = 0;
    int change_count = 0;
    
#if NCD_PLATFORM_WINDOWS
    while (1) {
#else
    while (g_watch_running) {
#endif
        NcdIpcClient *client = ipc_connect_with_timeout(5000);
        if (!client) {
            printf("[DISCONNECTED] Service not running\n");
#if NCD_PLATFORM_WINDOWS
            Sleep(opts->interval_ms);
            continue;
#else
            usleep(opts->interval_ms * 1000);
            continue;
#endif
        }
        
        NcdIpcStateInfo info;
        NcdIpcResult result = ipc_client_get_state_info(client, &info);
        
        if (result == NCD_IPC_OK) {
            /* Check for changes */
            if (last_meta_gen != 0 && last_db_gen != 0) {
                if (info.meta_generation != last_meta_gen) {
                    printf("[CHANGE %d] Metadata generation: %llu -> %llu\n",
                           ++change_count,
                           (unsigned long long)last_meta_gen,
                           (unsigned long long)info.meta_generation);
                }
                if (info.db_generation != last_db_gen) {
                    printf("[CHANGE %d] Database generation: %llu -> %llu\n",
                           ++change_count,
                           (unsigned long long)last_db_gen,
                           (unsigned long long)info.db_generation);
                }
            } else {
                printf("[INITIAL] Meta_gen=%llu, DB_gen=%llu\n",
                       (unsigned long long)info.meta_generation,
                       (unsigned long long)info.db_generation);
            }
            
            last_meta_gen = info.meta_generation;
            last_db_gen = info.db_generation;
        } else {
            printf("[ERROR] %s\n", ipc_error_string(result));
        }
        
        ipc_client_disconnect(client);
        
#if NCD_PLATFORM_WINDOWS
        Sleep(opts->interval_ms);
#else
        usleep(opts->interval_ms * 1000);
#endif
    }
    
    printf("\nWatch mode stopped. Total changes detected: %d\n", change_count);
    return IPC_EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    IpcStateOptions opts;
    
    if (!parse_state_args(argc, argv, &opts)) {
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
    
    print_test_header("State");
    
    if (opts.verbose) {
        printf("Mode:       %s\n", opts.watch ? "watch" : "single");
        if (opts.watch) printf("Interval:   %d ms\n", opts.interval_ms);
        printf("Format:     %s\n\n", opts.json ? "JSON" : "human-readable");
    }
    
    int result = IPC_EXIT_SUCCESS;
    
    if (opts.watch) {
        result = watch_mode(&opts);
    } else {
        if (opts.all || opts.info) {
            if (opts.verbose && !opts.json) {
                printf("=== State Information ===\n");
            }
            result = show_info(opts.json, opts.verbose);
            if (result != IPC_EXIT_SUCCESS) {
                ipc_client_cleanup();
                return result;
            }
            
            if (opts.all) {
                if (!opts.json) printf("\n");
            }
        }
        
        if (opts.all || opts.version) {
            if (opts.verbose && !opts.json) {
                printf("=== Version Information ===\n");
            }
            result = show_version(opts.json, opts.verbose);
        }
    }
    
    ipc_client_cleanup();
    return result;
}
