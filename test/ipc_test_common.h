/*
 * ipc_test_common.h -- Common utilities for IPC test programs
 *
 * Shared infrastructure for standalone IPC diagnostic tools.
 */

#ifndef IPC_TEST_COMMON_H
#define IPC_TEST_COMMON_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "../src/ncd.h"
#include "../src/control_ipc.h"

/* Version of the IPC test suite */
#define IPC_TEST_VERSION "1.0"

/* Exit codes matching the plan */
#define IPC_EXIT_SUCCESS      0
#define IPC_EXIT_NOT_RUNNING  1
#define IPC_EXIT_TIMEOUT      2
#define IPC_EXIT_INVALID      3
#define IPC_EXIT_BUSY         4
#define IPC_EXIT_REJECTED     5
#define IPC_EXIT_ERROR        6

/* Timing utilities */
#if NCD_PLATFORM_WINDOWS
#include <windows.h>
typedef LARGE_INTEGER ipc_time_t;
#else
#include <sys/time.h>
typedef struct timeval ipc_time_t;
#endif

/* Get current time */
void ipc_get_time(ipc_time_t *t);

/* Get elapsed time in milliseconds */
double ipc_elapsed_ms(ipc_time_t *start, ipc_time_t *end);

/* Get elapsed time in microseconds */
double ipc_elapsed_us(ipc_time_t *start, ipc_time_t *end);

/* Common argument parsing helpers */
typedef struct {
    bool help;
    bool verbose;
    bool json;
    bool once;
    bool continuous;
    int interval_ms;
    int count;
    int timeout_ms;
} IpcPingOptions;

typedef struct {
    bool help;
    bool verbose;
    bool json;
    bool info;
    bool version;
    bool all;
    bool watch;
    int interval_ms;
} IpcStateOptions;

typedef struct {
    bool help;
    bool verbose;
    bool dry_run;
    bool verify;
    int update_type;
    char group_name[256];
    char path[1024];
    char pattern[256];
    char key[256];
    char value[256];
    int history_index;
    int encoding; /* 1 = utf8, 2 = utf16 */
} IpcMetadataOptions;

typedef struct {
    bool help;
    bool verbose;
    char search[256];
    char target[1024];
    char file[1024];
    bool perf_test;
    bool stress;
    int count;
    int duration;
    int threads;
} IpcHeuristicOptions;

typedef struct {
    bool help;
    bool verbose;
    bool full;
    bool wait;
    bool status;
    char drives[64];
    char path[1024];
    int timeout_sec;
} IpcRescanOptions;

typedef struct {
    bool help;
    bool verbose;
    bool verify;
    bool timing;
    bool force;
} IpcFlushOptions;

typedef struct {
    bool help;
    bool verbose;
    int timeout_sec;
    bool force;
    bool verify;
} IpcShutdownOptions;

/* Parse common ping arguments. Returns false if should exit. */
bool parse_ping_args(int argc, char **argv, IpcPingOptions *opts);

/* Parse common state arguments. Returns false if should exit. */
bool parse_state_args(int argc, char **argv, IpcStateOptions *opts);

/* Parse common metadata arguments. Returns false if should exit. */
bool parse_metadata_args(int argc, char **argv, IpcMetadataOptions *opts);

/* Parse common heuristic arguments. Returns false if should exit. */
bool parse_heuristic_args(int argc, char **argv, IpcHeuristicOptions *opts);

/* Parse common rescan arguments. Returns false if should exit. */
bool parse_rescan_args(int argc, char **argv, IpcRescanOptions *opts);

/* Parse common flush arguments. Returns false if should exit. */
bool parse_flush_args(int argc, char **argv, IpcFlushOptions *opts);

/* Parse common shutdown arguments. Returns false if should exit. */
bool parse_shutdown_args(int argc, char **argv, IpcShutdownOptions *opts);

/* Print common header for test programs */
void print_test_header(const char *name);

/* Print help footer */
void print_help_footer(void);

/* Convert IPC result to exit code */
int ipc_result_to_exit_code(NcdIpcResult result);

/* Print formatted JSON for state info */
void print_json_state_info(const NcdIpcStateInfo *info);

/* Print formatted JSON for version info */
void print_json_version_info(const NcdIpcVersionInfo *info);

/* Print formatted state info (human readable) */
void print_human_state_info(const NcdIpcStateInfo *info);

/* Print formatted version info (human readable) */
void print_human_version_info(const NcdIpcVersionInfo *info);

/* Connect to service with timeout. Returns NULL on failure. */
NcdIpcClient *ipc_connect_with_timeout(int timeout_ms);

/* Check if service is running, print message if not */
bool check_service_running(void);

/* Get text encoding name */
const char *get_encoding_name(int encoding);

#endif /* IPC_TEST_COMMON_H */
