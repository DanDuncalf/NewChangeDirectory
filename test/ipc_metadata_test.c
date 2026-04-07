/*
 * ipc_metadata_test.c -- Test metadata update operations
 *
 * Usage: ipc_metadata_test <operation> [args...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_test_common.h"
#include "../src/control_ipc.h"

static void print_usage(const char *prog) {
    printf("Usage: %s <operation> [args...] [options]\n\n", prog);
    printf("Operations:\n");
    printf("  --group-add @name <path>          Add directory to group\n");
    printf("  --group-remove @name              Remove group\n");
    printf("  --group-remove-path @name <path>  Remove specific path from group\n");
    printf("  --exclusion-add <pattern>         Add exclusion pattern\n");
    printf("  --exclusion-remove <pattern>      Remove exclusion pattern\n");
    printf("  --config <key>=<value>            Update config setting\n");
    printf("  --clear-history                   Clear directory history\n");
    printf("  --history-add <path>              Add path to history\n");
    printf("  --history-remove <index>          Remove history entry by index\n");
    printf("  --history-swap                    Swap first two history entries\n");
    printf("  --encoding <utf8|utf16>           Switch text encoding\n");
    printf("\nOptions:\n");
    printf("  --dry-run, -n                     Show what would be done\n");
    printf("  --verbose, -v                     Show verbose output\n");
    printf("  --help, -h                        Show this help\n");
    print_help_footer();
}

int main(int argc, char **argv) {
    IpcMetadataOptions opts;
    
    if (!parse_metadata_args(argc, argv, &opts)) {
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
    
    print_test_header("Metadata");
    
    /* Check if service is running */
    if (!check_service_running()) {
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    printf("Service: RUNNING\n\n");
    
    if (opts.verbose) {
        printf("Operation:  ");
        switch (opts.update_type) {
            case NCD_META_UPDATE_GROUP_ADD: printf("GROUP_ADD\n"); break;
            case NCD_META_UPDATE_GROUP_REMOVE: printf("GROUP_REMOVE\n"); break;
            case NCD_META_UPDATE_EXCLUSION_ADD: printf("EXCLUSION_ADD\n"); break;
            case NCD_META_UPDATE_EXCLUSION_REMOVE: printf("EXCLUSION_REMOVE\n"); break;
            case NCD_META_UPDATE_CONFIG: printf("CONFIG\n"); break;
            case NCD_META_UPDATE_CLEAR_HISTORY: printf("CLEAR_HISTORY\n"); break;
            case NCD_META_UPDATE_DIR_HISTORY_ADD: printf("DIR_HISTORY_ADD\n"); break;
            case NCD_META_UPDATE_DIR_HISTORY_REMOVE: printf("DIR_HISTORY_REMOVE\n"); break;
            case NCD_META_UPDATE_DIR_HISTORY_SWAP: printf("DIR_HISTORY_SWAP\n"); break;
            case NCD_META_UPDATE_ENCODING_SWITCH: printf("ENCODING_SWITCH\n"); break;
            default: printf("UNKNOWN\n"); break;
        }
        
        if (opts.group_name[0]) printf("Group:      %s\n", opts.group_name);
        if (opts.path[0]) printf("Path:       %s\n", opts.path);
        if (opts.pattern[0]) printf("Pattern:    %s\n", opts.pattern);
        if (opts.key[0]) printf("Key:        %s\n", opts.key);
        if (opts.value[0]) printf("Value:      %s\n", opts.value);
        if (opts.update_type == NCD_META_UPDATE_DIR_HISTORY_REMOVE) {
            printf("Index:      %d\n", opts.history_index);
        }
        if (opts.update_type == NCD_META_UPDATE_ENCODING_SWITCH) {
            printf("Encoding:   %s\n", opts.encoding == 1 ? "UTF-8" : "UTF-16");
        }
        printf("\n");
    }
    
    /* Dry-run mode */
    if (opts.dry_run) {
        printf("DRY RUN: Would submit metadata update (type=%d)\n", opts.update_type);
        ipc_client_cleanup();
        return IPC_EXIT_SUCCESS;
    }
    
    /* Connect to service */
    NcdIpcClient *client = ipc_connect_with_timeout(5000);
    if (!client) {
        printf("Error: Failed to connect to service\n");
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    /* Build payload based on operation type */
    uint8_t payload_buf[4096];
    size_t payload_len = 0;
    
    switch (opts.update_type) {
        case NCD_META_UPDATE_GROUP_ADD: {
            /* Format: @name\0path\0 */
            char *p = (char *)payload_buf;
            size_t name_len = strlen(opts.group_name) + 1;
            size_t path_len = strlen(opts.path) + 1;
            memcpy(p, opts.group_name, name_len);
            memcpy(p + name_len, opts.path, path_len);
            payload_len = name_len + path_len;
            printf("Adding '%s' to group %s...\n", opts.path, opts.group_name);
            break;
        }
        case NCD_META_UPDATE_GROUP_REMOVE: {
            /* Format: @name\0[path\0] - path is optional */
            char *p = (char *)payload_buf;
            size_t name_len = strlen(opts.group_name) + 1;
            memcpy(p, opts.group_name, name_len);
            payload_len = name_len;
            if (opts.path[0]) {
                size_t path_len = strlen(opts.path) + 1;
                memcpy(p + name_len, opts.path, path_len);
                payload_len += path_len;
                printf("Removing path '%s' from group %s...\n", opts.path, opts.group_name);
            } else {
                printf("Removing group %s...\n", opts.group_name);
            }
            break;
        }
        case NCD_META_UPDATE_EXCLUSION_ADD:
        case NCD_META_UPDATE_EXCLUSION_REMOVE: {
            /* Format: pattern\0 */
            size_t pat_len = strlen(opts.pattern) + 1;
            memcpy(payload_buf, opts.pattern, pat_len);
            payload_len = pat_len;
            printf("%s exclusion '%s'...\n",
                   opts.update_type == NCD_META_UPDATE_EXCLUSION_ADD ? "Adding" : "Removing",
                   opts.pattern);
            break;
        }
        case NCD_META_UPDATE_CONFIG: {
            /* Format: key\0value\0 */
            char *p = (char *)payload_buf;
            size_t key_len = strlen(opts.key) + 1;
            size_t val_len = strlen(opts.value) + 1;
            memcpy(p, opts.key, key_len);
            memcpy(p + key_len, opts.value, val_len);
            payload_len = key_len + val_len;
            printf("Setting config %s=%s...\n", opts.key, opts.value);
            break;
        }
        case NCD_META_UPDATE_CLEAR_HISTORY: {
            printf("Clearing directory history...\n");
            payload_len = 0;
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_ADD: {
            /* Format: path\0 */
            size_t path_len = strlen(opts.path) + 1;
            memcpy(payload_buf, opts.path, path_len);
            payload_len = path_len;
            printf("Adding '%s' to history...\n", opts.path);
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_REMOVE: {
            /* Format: index as 4-byte int */
            int32_t idx = opts.history_index;
            memcpy(payload_buf, &idx, sizeof(idx));
            payload_len = sizeof(idx);
            printf("Removing history entry at index %d...\n", opts.history_index);
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_SWAP: {
            printf("Swapping first two history entries...\n");
            payload_len = 0;
            break;
        }
        case NCD_META_UPDATE_ENCODING_SWITCH: {
            /* Format: encoding as 1-byte int */
            uint8_t enc = (uint8_t)opts.encoding;
            memcpy(payload_buf, &enc, sizeof(enc));
            payload_len = sizeof(enc);
            printf("Switching encoding to %s...\n", opts.encoding == 1 ? "UTF-8" : "UTF-16");
            break;
        }
        default:
            printf("Error: Unknown update type\n");
            ipc_client_disconnect(client);
            ipc_client_cleanup();
            return IPC_EXIT_INVALID;
    }
    
    /* Send the metadata update */
    ipc_time_t start, end;
    ipc_get_time(&start);
    
    NcdIpcResult result = ipc_client_submit_metadata(client, opts.update_type,
                                                      payload_len > 0 ? payload_buf : NULL,
                                                      payload_len);
    
    ipc_get_time(&end);
    double elapsed_ms = ipc_elapsed_ms(&start, &end);
    
    ipc_client_disconnect(client);
    
    printf("\n");
    if (result == NCD_IPC_OK) {
        printf("Result:     ACCEPTED (queued for processing)\n");
        printf("Time:       %.2f ms\n", elapsed_ms);
        printf("Exit code:  0\n");
        ipc_client_cleanup();
        return IPC_EXIT_SUCCESS;
    } else if (result == NCD_IPC_ERROR_BUSY || result == NCD_IPC_ERROR_BUSY_LOADING ||
               result == NCD_IPC_ERROR_BUSY_SCANNING) {
        printf("Result:     BUSY\n");
        printf("Time:       %.2f ms\n", elapsed_ms);
        printf("Exit code:  4\n");
        ipc_client_cleanup();
        return IPC_EXIT_BUSY;
    } else if (result == NCD_IPC_ERROR_INVALID) {
        printf("Result:     INVALID PARAMETER\n");
        printf("Time:       %.2f ms\n", elapsed_ms);
        printf("Exit code:  3\n");
        ipc_client_cleanup();
        return IPC_EXIT_INVALID;
    } else {
        printf("Result:     REJECTED (%s)\n", ipc_error_string(result));
        printf("Time:       %.2f ms\n", elapsed_ms);
        printf("Exit code:  5\n");
        ipc_client_cleanup();
        return IPC_EXIT_REJECTED;
    }
}
