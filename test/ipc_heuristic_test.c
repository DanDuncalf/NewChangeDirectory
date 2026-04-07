/*
 * ipc_heuristic_test.c -- Test heuristic update submissions
 *
 * Usage: ipc_heuristic_test --search "term" --path "/result/path"
 *        ipc_heuristic_test --file heuristics.txt
 *        ipc_heuristic_test --perf-test --count <n>
 *        ipc_heuristic_test --stress --duration <sec> --threads <n>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_test_common.h"
#include "../src/control_ipc.h"

#if !NCD_PLATFORM_WINDOWS
#include <pthread.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --search, -s <term>       Search term\n");
    printf("  --path, -p <path>         Target path result\n");
    printf("  --file, -f <path>         Batch from file (format: search|path per line)\n");
    printf("  --perf-test               Performance test mode\n");
    printf("  --stress                  Stress test mode\n");
    printf("  --count, -c <n>           Number of iterations (default: 1)\n");
    printf("  --duration, -d <sec>      Stress test duration (default: 10)\n");
    printf("  --threads, -t <n>         Number of threads for stress test (default: 4)\n");
    printf("  --verbose, -v             Show verbose output\n");
    printf("  --help, -h                Show this help\n");
    print_help_footer();
}

typedef struct {
    char search[256];
    char path[1024];
} HeuristicEntry;

static int load_heuristics_file(const char *filename, HeuristicEntry **out_entries, int *out_count) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return -1;
    }
    
    int capacity = 100;
    int count = 0;
    HeuristicEntry *entries = malloc(capacity * sizeof(HeuristicEntry));
    if (!entries) {
        fclose(f);
        return -1;
    }
    
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len > 0 && line[len-1] == '\r') {
            line[--len] = '\0';
        }
        
        /* Skip empty lines and comments */
        if (len == 0 || line[0] == '#') continue;
        
        /* Parse search|path format */
        char *sep = strchr(line, '|');
        if (!sep) {
            fprintf(stderr, "Warning: Invalid line format (expected 'search|path'): %s\n", line);
            continue;
        }
        
        if (count >= capacity) {
            capacity *= 2;
            HeuristicEntry *new_entries = realloc(entries, capacity * sizeof(HeuristicEntry));
            if (!new_entries) {
                free(entries);
                fclose(f);
                return -1;
            }
            entries = new_entries;
        }
        
        *sep = '\0';
        strncpy(entries[count].search, line, sizeof(entries[count].search) - 1);
        strncpy(entries[count].path, sep + 1, sizeof(entries[count].path) - 1);
        entries[count].search[sizeof(entries[count].search) - 1] = '\0';
        entries[count].path[sizeof(entries[count].path) - 1] = '\0';
        count++;
    }
    
    fclose(f);
    *out_entries = entries;
    *out_count = count;
    return 0;
}

static int submit_single_heuristic(const char *search, const char *path, bool verbose) {
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        if (verbose) printf("  Connection failed\n");
        return -1;
    }
    
    NcdIpcResult result = ipc_client_submit_heuristic(client, search, path);
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        if (verbose) printf("  Failed: %s\n", ipc_error_string(result));
        return -1;
    }
    
    return 0;
}

static int run_single_submission(const IpcHeuristicOptions *opts) {
    printf("Submitting heuristic: \"%s\" -> \"%s\"\n", opts->search, opts->target);
    
    int result = submit_single_heuristic(opts->search, opts->target, opts->verbose);
    
    if (result == 0) {
        printf("Result: ACCEPTED (queued for processing)\n");
        printf("Exit code: 0\n");
        return IPC_EXIT_SUCCESS;
    } else {
        printf("Result: FAILED\n");
        printf("Exit code: 1\n");
        return IPC_EXIT_ERROR;
    }
}

static int run_batch_from_file(const IpcHeuristicOptions *opts) {
    HeuristicEntry *entries = NULL;
    int count = 0;
    
    if (load_heuristics_file(opts->file, &entries, &count) != 0) {
        return IPC_EXIT_ERROR;
    }
    
    printf("Loaded %d heuristics from '%s'\n\n", count, opts->file);
    
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < count; i++) {
        if (opts->verbose) {
            printf("[%d/%d] \"%s\" -> \"%s\"\n", i + 1, count, entries[i].search, entries[i].path);
        }
        
        if (submit_single_heuristic(entries[i].search, entries[i].path, opts->verbose) == 0) {
            success++;
        } else {
            failed++;
        }
    }
    
    printf("\n-------------------------\n");
    printf("Submitted:  %d\n", success);
    printf("Failed:     %d\n", failed);
    printf("Total:      %d\n", count);
    
    free(entries);
    
    return failed == 0 ? IPC_EXIT_SUCCESS : IPC_EXIT_ERROR;
}

static int run_perf_test(const IpcHeuristicOptions *opts) {
    printf("Performance test: %d iterations\n\n", opts->count);
    
    ipc_time_t start, end;
    ipc_get_time(&start);
    
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < opts->count; i++) {
        char search[64];
        char path[256];
        snprintf(search, sizeof(search), "perf_test_%d", i);
        snprintf(path, sizeof(path), "/test/path/%d", i);
        
        if (submit_single_heuristic(search, path, false) == 0) {
            success++;
        } else {
            failed++;
        }
        
        if (opts->verbose && (i + 1) % 100 == 0) {
            printf("Progress: %d/%d\n", i + 1, opts->count);
        }
    }
    
    ipc_get_time(&end);
    double elapsed_ms = ipc_elapsed_ms(&start, &end);
    
    printf("\n-------------------------\n");
    printf("Iterations: %d\n", opts->count);
    printf("Successful: %d\n", success);
    printf("Failed:     %d\n", failed);
    printf("Total time: %.2f ms\n", elapsed_ms);
    printf("Avg time:   %.3f ms\n", elapsed_ms / opts->count);
    printf("Rate:       %.1f req/sec\n", (double)opts->count * 1000.0 / elapsed_ms);
    
    return failed == 0 ? IPC_EXIT_SUCCESS : IPC_EXIT_ERROR;
}

#if !NCD_PLATFORM_WINDOWS
typedef struct {
    int thread_id;
    int iterations;
    int *success_count;
    int *fail_count;
    pthread_mutex_t *mutex;
} StressThreadArgs;

static void *stress_thread(void *arg) {
    StressThreadArgs *args = (StressThreadArgs *)arg;
    int local_success = 0;
    int local_fail = 0;
    
    for (int i = 0; i < args->iterations; i++) {
        char search[64];
        char path[256];
        snprintf(search, sizeof(search), "stress_t%d_%d", args->thread_id, i);
        snprintf(path, sizeof(path), "/test/thread%d/path%d", args->thread_id, i);
        
        if (submit_single_heuristic(search, path, false) == 0) {
            local_success++;
        } else {
            local_fail++;
        }
    }
    
    pthread_mutex_lock(args->mutex);
    *args->success_count += local_success;
    *args->fail_count += local_fail;
    pthread_mutex_unlock(args->mutex);
    
    return NULL;
}
#endif

static int run_stress_test(const IpcHeuristicOptions *opts) {
    printf("Stress test: %d threads, %d seconds\n\n", opts->threads, opts->duration);
    
    /* Estimate iterations (rough guess: ~1000 req/sec per thread) */
    int iterations_per_thread = opts->duration * 1000;
    
#if NCD_PLATFORM_WINDOWS
    printf("Stress test not implemented on Windows yet.\n");
    printf("Using single-threaded fallback...\n\n");
    
    ipc_time_t start, end;
    ipc_get_time(&start);
    
    int total_iterations = opts->threads * iterations_per_thread;
    int success = 0;
    int failed = 0;
    
    for (int i = 0; i < total_iterations; i++) {
        char search[64];
        char path[256];
        snprintf(search, sizeof(search), "stress_%d", i);
        snprintf(path, sizeof(path), "/test/path%d", i);
        
        if (submit_single_heuristic(search, path, false) == 0) {
            success++;
        } else {
            failed++;
        }
        
        if ((i + 1) % 1000 == 0) {
            printf("Progress: %d/%d\n", i + 1, total_iterations);
        }
    }
    
    ipc_get_time(&end);
    double elapsed_ms = ipc_elapsed_ms(&start, &end);
    
    printf("\n-------------------------\n");
    printf("Iterations: %d\n", total_iterations);
    printf("Successful: %d\n", success);
    printf("Failed:     %d\n", failed);
    printf("Total time: %.2f ms\n", elapsed_ms);
    printf("Rate:       %.1f req/sec\n", (double)total_iterations * 1000.0 / elapsed_ms);
    
    return failed == 0 ? IPC_EXIT_SUCCESS : IPC_EXIT_ERROR;
    
#else
    /* POSIX: use pthreads */
    pthread_t *threads = malloc(opts->threads * sizeof(pthread_t));
    StressThreadArgs *args = malloc(opts->threads * sizeof(StressThreadArgs));
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int total_success = 0;
    int total_fail = 0;
    
    ipc_time_t start, end;
    ipc_get_time(&start);
    
    for (int i = 0; i < opts->threads; i++) {
        args[i].thread_id = i;
        args[i].iterations = iterations_per_thread;
        args[i].success_count = &total_success;
        args[i].fail_count = &total_fail;
        args[i].mutex = &mutex;
        pthread_create(&threads[i], NULL, stress_thread, &args[i]);
    }
    
    for (int i = 0; i < opts->threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    ipc_get_time(&end);
    double elapsed_ms = ipc_elapsed_ms(&start, &end);
    
    printf("\n-------------------------\n");
    printf("Threads:    %d\n", opts->threads);
    printf("Iterations: %d\n", opts->threads * iterations_per_thread);
    printf("Successful: %d\n", total_success);
    printf("Failed:     %d\n", total_fail);
    printf("Total time: %.2f ms\n", elapsed_ms);
    printf("Rate:       %.1f req/sec\n", 
           (double)(opts->threads * iterations_per_thread) * 1000.0 / elapsed_ms);
    
    free(threads);
    free(args);
    pthread_mutex_destroy(&mutex);
    
    return total_fail == 0 ? IPC_EXIT_SUCCESS : IPC_EXIT_ERROR;
#endif
}

int main(int argc, char **argv) {
    IpcHeuristicOptions opts;
    
    if (!parse_heuristic_args(argc, argv, &opts)) {
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
    
    print_test_header("Heuristic");
    
    /* Check if service is running */
    if (!check_service_running()) {
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    printf("Service: RUNNING\n\n");
    
    int result;
    
    if (opts.perf_test) {
        result = run_perf_test(&opts);
    } else if (opts.stress) {
        result = run_stress_test(&opts);
    } else if (opts.file[0]) {
        result = run_batch_from_file(&opts);
    } else {
        result = run_single_submission(&opts);
    }
    
    ipc_client_cleanup();
    return result;
}
