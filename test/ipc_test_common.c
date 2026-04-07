/*
 * ipc_test_common.c -- Common utilities for IPC test programs
 */

#include "ipc_test_common.h"
#include <string.h>
#include <stdlib.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ================================================================ Timing utilities */

void ipc_get_time(ipc_time_t *t) {
#if NCD_PLATFORM_WINDOWS
    QueryPerformanceCounter(t);
#else
    gettimeofday(t, NULL);
#endif
}

double ipc_elapsed_ms(ipc_time_t *start, ipc_time_t *end) {
#if NCD_PLATFORM_WINDOWS
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return ((double)(end->QuadPart - start->QuadPart) * 1000.0) / (double)freq.QuadPart;
#else
    return ((double)(end->tv_sec - start->tv_sec) * 1000.0) +
           ((double)(end->tv_usec - start->tv_usec) / 1000.0);
#endif
}

double ipc_elapsed_us(ipc_time_t *start, ipc_time_t *end) {
#if NCD_PLATFORM_WINDOWS
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return ((double)(end->QuadPart - start->QuadPart) * 1000000.0) / (double)freq.QuadPart;
#else
    return ((double)(end->tv_sec - start->tv_sec) * 1000000.0) +
           ((double)(end->tv_usec - start->tv_usec));
#endif
}

/* ================================================================ Header/Footer */

void print_test_header(const char *name) {
    printf("NCD Service IPC %s Test v%s\n", name, IPC_TEST_VERSION);
    printf("=====================================\n\n");
}

void print_help_footer(void) {
    printf("\nExit codes:\n");
    printf("  0 = Success\n");
    printf("  1 = Service not running\n");
    printf("  2 = Timeout\n");
    printf("  3 = Invalid response\n");
    printf("  4 = Service busy\n");
    printf("  5 = Request rejected\n");
    printf("  6 = Generic error\n");
}

/* ================================================================ Argument parsing helpers */

static bool parse_int(const char *s, int *out) {
    char *end;
    long val = strtol(s, &end, 10);
    if (*end != '\0' || end == s) return false;
    *out = (int)val;
    return true;
}

/* ================================================================ Ping argument parsing */

bool parse_ping_args(int argc, char **argv, IpcPingOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->interval_ms = 1000;  /* Default: 1 second */
    opts->count = 0;           /* Default: run once or continuous */
    opts->timeout_ms = 5000;   /* Default: 5 seconds */
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;  /* Exit after showing help */
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--once") == 0) {
            opts->once = true;
        } else if (strcmp(argv[i], "--continuous") == 0) {
            opts->continuous = true;
        } else if (strcmp(argv[i], "--interval") == 0 || strcmp(argv[i], "-i") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->interval_ms) || opts->interval_ms < 1) {
                fprintf(stderr, "Error: --interval requires a positive number (ms)\n");
                return false;
            }
        } else if (strcmp(argv[i], "--count") == 0 || strcmp(argv[i], "-c") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->count) || opts->count < 1) {
                fprintf(stderr, "Error: --count requires a positive number\n");
                return false;
            }
        } else if (strcmp(argv[i], "--timeout") == 0 || strcmp(argv[i], "-t") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->timeout_ms) || opts->timeout_ms < 1) {
                fprintf(stderr, "Error: --timeout requires a positive number (ms)\n");
                return false;
            }
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    /* Validate options */
    if (opts->continuous && opts->count > 0) {
        fprintf(stderr, "Error: --continuous and --count are mutually exclusive\n");
        return false;
    }
    if (!opts->continuous && !opts->once && opts->count == 0) {
        opts->once = true;  /* Default to once if neither specified */
    }
    
    return true;
}

/* ================================================================ State argument parsing */

bool parse_state_args(int argc, char **argv, IpcStateOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->interval_ms = 1000;  /* Default: 1 second for watch */
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
            opts->json = true;
        } else if (strcmp(argv[i], "--info") == 0) {
            opts->info = true;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            opts->version = true;
        } else if (strcmp(argv[i], "--all") == 0 || strcmp(argv[i], "-a") == 0) {
            opts->all = true;
        } else if (strcmp(argv[i], "--watch") == 0 || strcmp(argv[i], "-w") == 0) {
            opts->watch = true;
        } else if (strcmp(argv[i], "--interval") == 0 || strcmp(argv[i], "-i") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->interval_ms) || opts->interval_ms < 100) {
                fprintf(stderr, "Error: --interval requires a positive number >= 100 (ms)\n");
                return false;
            }
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    /* Default to --all if nothing specified */
    if (!opts->info && !opts->version && !opts->all) {
        opts->all = true;
    }
    
    return true;
}

/* ================================================================ Metadata argument parsing */

bool parse_metadata_args(int argc, char **argv, IpcMetadataOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->update_type = -1;  /* Not set */
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "-n") == 0) {
            opts->dry_run = true;
        } else if (strcmp(argv[i], "--verify") == 0) {
            opts->verify = true;
        } else if (strcmp(argv[i], "--group-add") == 0) {
            opts->update_type = NCD_META_UPDATE_GROUP_ADD;
            if (++i >= argc) {
                fprintf(stderr, "Error: --group-add requires @name and path arguments\n");
                return false;
            }
            /* Parse @name */
            if (argv[i][0] != '@') {
                fprintf(stderr, "Error: Group name must start with @\n");
                return false;
            }
            strncpy(opts->group_name, argv[i], sizeof(opts->group_name) - 1);
            /* Parse path */
            if (++i >= argc) {
                fprintf(stderr, "Error: --group-add requires a path argument\n");
                return false;
            }
            strncpy(opts->path, argv[i], sizeof(opts->path) - 1);
        } else if (strcmp(argv[i], "--group-remove") == 0) {
            opts->update_type = NCD_META_UPDATE_GROUP_REMOVE;
            if (++i >= argc || argv[i][0] != '@') {
                fprintf(stderr, "Error: --group-remove requires @name argument\n");
                return false;
            }
            strncpy(opts->group_name, argv[i], sizeof(opts->group_name) - 1);
        } else if (strcmp(argv[i], "--group-remove-path") == 0) {
            opts->update_type = NCD_META_UPDATE_GROUP_REMOVE;
            if (++i >= argc || argv[i][0] != '@') {
                fprintf(stderr, "Error: --group-remove-path requires @name argument\n");
                return false;
            }
            strncpy(opts->group_name, argv[i], sizeof(opts->group_name) - 1);
            if (++i >= argc) {
                fprintf(stderr, "Error: --group-remove-path requires a path argument\n");
                return false;
            }
            strncpy(opts->path, argv[i], sizeof(opts->path) - 1);
        } else if (strcmp(argv[i], "--exclusion-add") == 0) {
            opts->update_type = NCD_META_UPDATE_EXCLUSION_ADD;
            if (++i >= argc) {
                fprintf(stderr, "Error: --exclusion-add requires a pattern argument\n");
                return false;
            }
            strncpy(opts->pattern, argv[i], sizeof(opts->pattern) - 1);
        } else if (strcmp(argv[i], "--exclusion-remove") == 0) {
            opts->update_type = NCD_META_UPDATE_EXCLUSION_REMOVE;
            if (++i >= argc) {
                fprintf(stderr, "Error: --exclusion-remove requires a pattern argument\n");
                return false;
            }
            strncpy(opts->pattern, argv[i], sizeof(opts->pattern) - 1);
        } else if (strcmp(argv[i], "--config") == 0) {
            opts->update_type = NCD_META_UPDATE_CONFIG;
            if (++i >= argc) {
                fprintf(stderr, "Error: --config requires key=value argument\n");
                return false;
            }
            char *eq = strchr(argv[i], '=');
            if (!eq) {
                fprintf(stderr, "Error: --config requires key=value format\n");
                return false;
            }
            size_t key_len = eq - argv[i];
            if (key_len >= sizeof(opts->key)) key_len = sizeof(opts->key) - 1;
            strncpy(opts->key, argv[i], key_len);
            opts->key[key_len] = '\0';
            strncpy(opts->value, eq + 1, sizeof(opts->value) - 1);
        } else if (strcmp(argv[i], "--clear-history") == 0) {
            opts->update_type = NCD_META_UPDATE_CLEAR_HISTORY;
        } else if (strcmp(argv[i], "--history-add") == 0) {
            opts->update_type = NCD_META_UPDATE_DIR_HISTORY_ADD;
            if (++i >= argc) {
                fprintf(stderr, "Error: --history-add requires a path argument\n");
                return false;
            }
            strncpy(opts->path, argv[i], sizeof(opts->path) - 1);
        } else if (strcmp(argv[i], "--history-remove") == 0) {
            opts->update_type = NCD_META_UPDATE_DIR_HISTORY_REMOVE;
            if (++i >= argc || !parse_int(argv[i], &opts->history_index)) {
                fprintf(stderr, "Error: --history-remove requires an index argument\n");
                return false;
            }
        } else if (strcmp(argv[i], "--history-swap") == 0) {
            opts->update_type = NCD_META_UPDATE_DIR_HISTORY_SWAP;
        } else if (strcmp(argv[i], "--encoding") == 0) {
            opts->update_type = NCD_META_UPDATE_ENCODING_SWITCH;
            if (++i >= argc) {
                fprintf(stderr, "Error: --encoding requires utf8 or utf16 argument\n");
                return false;
            }
            if (strcmp(argv[i], "utf8") == 0) {
                opts->encoding = 1;
            } else if (strcmp(argv[i], "utf16") == 0) {
                opts->encoding = 2;
            } else {
                fprintf(stderr, "Error: --encoding must be utf8 or utf16\n");
                return false;
            }
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    if (opts->update_type < 0 && !opts->help) {
        fprintf(stderr, "Error: No update operation specified\n");
        return false;
    }
    
    return true;
}

/* ================================================================ Heuristic argument parsing */

bool parse_heuristic_args(int argc, char **argv, IpcHeuristicOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->count = 1;
    opts->duration = 10;
    opts->threads = 4;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--search") == 0 || strcmp(argv[i], "-s") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --search requires an argument\n");
                return false;
            }
            strncpy(opts->search, argv[i], sizeof(opts->search) - 1);
        } else if (strcmp(argv[i], "--path") == 0 || strcmp(argv[i], "-p") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --path requires an argument\n");
                return false;
            }
            strncpy(opts->target, argv[i], sizeof(opts->target) - 1);
        } else if (strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --file requires an argument\n");
                return false;
            }
            strncpy(opts->file, argv[i], sizeof(opts->file) - 1);
        } else if (strcmp(argv[i], "--perf-test") == 0) {
            opts->perf_test = true;
        } else if (strcmp(argv[i], "--stress") == 0) {
            opts->stress = true;
        } else if (strcmp(argv[i], "--count") == 0 || strcmp(argv[i], "-c") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->count) || opts->count < 1) {
                fprintf(stderr, "Error: --count requires a positive number\n");
                return false;
            }
        } else if (strcmp(argv[i], "--duration") == 0 || strcmp(argv[i], "-d") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->duration) || opts->duration < 1) {
                fprintf(stderr, "Error: --duration requires a positive number (seconds)\n");
                return false;
            }
        } else if (strcmp(argv[i], "--threads") == 0 || strcmp(argv[i], "-t") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->threads) || opts->threads < 1) {
                fprintf(stderr, "Error: --threads requires a positive number\n");
                return false;
            }
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    /* If not perf/stress mode, need search and path */
    if (!opts->perf_test && !opts->stress && !opts->help) {
        if (opts->search[0] == '\0' || opts->target[0] == '\0') {
            if (opts->file[0] == '\0') {
                fprintf(stderr, "Error: --search and --path (or --file) are required\n");
                return false;
            }
        }
    }
    
    return true;
}

/* ================================================================ Rescan argument parsing */

bool parse_rescan_args(int argc, char **argv, IpcRescanOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->timeout_sec = 300;  /* Default: 5 minutes */
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--full") == 0) {
            opts->full = true;
        } else if (strcmp(argv[i], "--drive") == 0 || strcmp(argv[i], "-d") == 0) {
            if (++i >= argc || strlen(argv[i]) != 1) {
                fprintf(stderr, "Error: --drive requires a single drive letter (e.g., C)\n");
                return false;
            }
            strncat(opts->drives, argv[i], 1);
        } else if (strcmp(argv[i], "--drives") == 0 || strcmp(argv[i], "-D") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --drives requires comma-separated drive letters (e.g., C,D,E)\n");
                return false;
            }
            strncpy(opts->drives, argv[i], sizeof(opts->drives) - 1);
        } else if (strcmp(argv[i], "--path") == 0 || strcmp(argv[i], "-p") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --path requires a path argument\n");
                return false;
            }
            strncpy(opts->path, argv[i], sizeof(opts->path) - 1);
        } else if (strcmp(argv[i], "--wait") == 0 || strcmp(argv[i], "-w") == 0) {
            opts->wait = true;
        } else if (strcmp(argv[i], "--status") == 0 || strcmp(argv[i], "-s") == 0) {
            opts->status = true;
        } else if (strcmp(argv[i], "--timeout") == 0 || strcmp(argv[i], "-t") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->timeout_sec) || opts->timeout_sec < 1) {
                fprintf(stderr, "Error: --timeout requires a positive number (seconds)\n");
                return false;
            }
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    /* Default to full if nothing specified */
    if (!opts->full && opts->drives[0] == '\0' && opts->path[0] == '\0' && !opts->status) {
        opts->full = true;
    }
    
    return true;
}

/* ================================================================ Flush argument parsing */

bool parse_flush_args(int argc, char **argv, IpcFlushOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--verify") == 0) {
            opts->verify = true;
        } else if (strcmp(argv[i], "--timing") == 0) {
            opts->timing = true;
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            opts->force = true;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    return true;
}

/* ================================================================ Shutdown argument parsing */

bool parse_shutdown_args(int argc, char **argv, IpcShutdownOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->timeout_sec = 30;  /* Default: 30 seconds */
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--timeout") == 0 || strcmp(argv[i], "-t") == 0) {
            if (++i >= argc || !parse_int(argv[i], &opts->timeout_sec) || opts->timeout_sec < 1) {
                fprintf(stderr, "Error: --timeout requires a positive number (seconds)\n");
                return false;
            }
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            opts->force = true;
        } else if (strcmp(argv[i], "--verify") == 0) {
            opts->verify = true;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    return true;
}

/* ==================================================== Result code conversion */

int ipc_result_to_exit_code(NcdIpcResult result) {
    switch (result) {
        case NCD_IPC_OK: return IPC_EXIT_SUCCESS;
        case NCD_IPC_ERROR_NOT_FOUND: return IPC_EXIT_NOT_RUNNING;
        case NCD_IPC_ERROR_BUSY:
        case NCD_IPC_ERROR_BUSY_LOADING:
        case NCD_IPC_ERROR_BUSY_SCANNING: return IPC_EXIT_BUSY;
        case NCD_IPC_ERROR_INVALID: return IPC_EXIT_INVALID;
        default: return IPC_EXIT_ERROR;
    }
}

/* ==================================================== Output formatting */

const char *get_encoding_name(int encoding) {
    switch (encoding) {
        case 1: return "UTF-8";
        case 2: return "UTF-16LE";
        default: return "Unknown";
    }
}

void print_json_state_info(const NcdIpcStateInfo *info) {
    printf("{\n");
    printf("  \"protocol_version\": %u,\n", info->protocol_version);
    printf("  \"text_encoding\": \"%s\",\n", get_encoding_name(info->text_encoding));
    printf("  \"meta_generation\": %llu,\n", (unsigned long long)info->meta_generation);
    printf("  \"db_generation\": %llu,\n", (unsigned long long)info->db_generation);
    printf("  \"meta_size\": %u,\n", info->meta_size);
    printf("  \"db_size\": %u,\n", info->db_size);
    printf("  \"meta_shm_name\": \"%s\",\n", info->meta_name);
    printf("  \"db_shm_name\": \"%s\"\n", info->db_name);
    printf("}\n");
}

void print_json_version_info(const NcdIpcVersionInfo *info) {
    printf("{\n");
    printf("  \"app_version\": \"%s\",\n", info->app_version);
    printf("  \"build_stamp\": \"%s\",\n", info->build_stamp);
    printf("  \"protocol_version\": %u\n", info->protocol_version);
    printf("}\n");
}

void print_human_state_info(const NcdIpcStateInfo *info) {
    printf("PROTOCOL:   %u\n", info->protocol_version);
    printf("ENCODING:   %s\n", get_encoding_name(info->text_encoding));
    printf("META_GEN:   %llu\n", (unsigned long long)info->meta_generation);
    printf("DB_GEN:     %llu\n", (unsigned long long)info->db_generation);
    printf("META_SIZE:  %u bytes\n", info->meta_size);
    printf("DB_SIZE:    %u bytes\n", info->db_size);
    printf("META_SHM:   %s\n", info->meta_name);
    printf("DB_SHM:     %s\n", info->db_name);
}

void print_human_version_info(const NcdIpcVersionInfo *info) {
    printf("APP VERSION:      %s\n", info->app_version);
    printf("BUILD STAMP:      %s\n", info->build_stamp);
    printf("PROTOCOL VERSION: %u\n", info->protocol_version);
}

/* ==================================================== Connection helpers */

NcdIpcClient *ipc_connect_with_timeout(int timeout_ms) {
    ipc_time_t start, now;
    ipc_get_time(&start);
    
    NcdIpcClient *client = NULL;
    while (client == NULL) {
        client = ipc_client_connect();
        if (client) break;
        
        ipc_get_time(&now);
        if (ipc_elapsed_ms(&start, &now) > timeout_ms) {
            break;
        }
        
#if NCD_PLATFORM_WINDOWS
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    
    return client;
}

bool check_service_running(void) {
    if (!ipc_service_exists()) {
        printf("Service: NOT RUNNING\n");
        return false;
    }
    return true;
}
