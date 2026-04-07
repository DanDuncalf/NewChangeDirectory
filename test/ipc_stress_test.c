/*
 * ipc_stress_test.c -- Load test the service IPC under heavy concurrency
 *
 * Usage: ipc_stress_test [--connections <n>] [--rate <req/sec>] [--duration <sec>] [--operation <type>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
    printf("  --connections, -c <n>   Number of concurrent connections (default: 10)\n");
    printf("  --rate, -r <req/sec>    Target request rate (default: unlimited)\n");
    printf("  --duration, -d <sec>    Test duration in seconds (default: 30)\n");
    printf("  --operation, -o <type>  Operation type: ping, state, version (default: ping)\n");
    printf("  --mixed                 Mix all operation types\n");
    printf("  --verbose, -v           Show verbose output\n");
    printf("  --help, -h              Show this help\n");
    print_help_footer();
}

typedef struct {
    int connections;
    int rate;
    int duration;
    char operation[32];
    bool mixed;
    bool verbose;
} StressOptions;

typedef struct {
    int thread_id;
    int iterations;
    int target_rate;  /* Requests per second per thread */
    char operation[32];
    double *latencies;
    int latency_count;
    int success_count;
    int error_count;
    bool mixed;
    bool *stop_flag;
} ThreadArgs;

static int parse_args(int argc, char **argv, StressOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->connections = 10;
    opts->rate = 0;  /* Unlimited */
    opts->duration = 30;
    strcpy(opts->operation, "ping");
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return -1;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--connections") == 0 || strcmp(argv[i], "-c") == 0) {
            if (++i >= argc || (opts->connections = atoi(argv[i])) < 1 || opts->connections > 100) {
                fprintf(stderr, "Error: --connections requires 1-100\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--rate") == 0 || strcmp(argv[i], "-r") == 0) {
            if (++i >= argc || (opts->rate = atoi(argv[i])) < 0) {
                fprintf(stderr, "Error: --rate requires a non-negative number\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--duration") == 0 || strcmp(argv[i], "-d") == 0) {
            if (++i >= argc || (opts->duration = atoi(argv[i])) < 1) {
                fprintf(stderr, "Error: --duration requires a positive number\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--operation") == 0 || strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --operation requires an argument\n");
                return 0;
            }
            strncpy(opts->operation, argv[i], sizeof(opts->operation) - 1);
        } else if (strcmp(argv[i], "--mixed") == 0) {
            opts->mixed = true;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return 0;
        }
    }
    
    return 1;
}

static double do_operation(const char *op, int thread_id, int iter) {
    ipc_time_t start, end;
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        return -1.0;
    }
    
    ipc_get_time(&start);
    
    NcdIpcResult result = NCD_IPC_ERROR_GENERIC;
    
    if (strcmp(op, "ping") == 0) {
        result = ipc_client_ping(client);
    } else if (strcmp(op, "state") == 0) {
        NcdIpcStateInfo info;
        result = ipc_client_get_state_info(client, &info);
    } else if (strcmp(op, "version") == 0) {
        NcdIpcVersionInfo info;
        result = ipc_client_get_version(client, &info);
    }
    
    ipc_get_time(&end);
    ipc_client_disconnect(client);
    
    if (result != NCD_IPC_OK) {
        return -1.0;
    }
    
    return ipc_elapsed_ms(&start, &end);
}

#if !NCD_PLATFORM_WINDOWS
typedef struct {
    ThreadArgs *args;
    int start_idx;
    int count;
} WorkerArgs;

static void *worker_thread(void *arg) {
    WorkerArgs *wa = (WorkerArgs *)arg;
    ThreadArgs *args = wa->args;
    
    int end_idx = wa->start_idx + wa->count;
    
    for (int i = wa->start_idx; i < end_idx && !*args->stop_flag; i++) {
        const char *op = args->mixed ? 
            (i % 3 == 0 ? "ping" : (i % 3 == 1 ? "state" : "version")) : args->operation;
        
        ipc_time_t iter_start;
        ipc_get_time(&iter_start);
        
        double latency = do_operation(op, args->thread_id, i);
        
        if (latency >= 0) {
            args->latencies[args->latency_count++] = latency;
            args->success_count++;
        } else {
            args->error_count++;
        }
        
        /* Rate limiting */
        if (args->target_rate > 0) {
            double target_interval = 1000.0 / args->target_rate;  /* ms */
            ipc_time_t now;
            ipc_get_time(&now);
            double elapsed = ipc_elapsed_ms(&iter_start, &now);
            if (elapsed < target_interval) {
                usleep((useconds_t)((target_interval - elapsed) * 1000));
            }
        }
    }
    
    return NULL;
}
#endif

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

int main(int argc, char **argv) {
    StressOptions opts;
    
    int parse_result = parse_args(argc, argv, &opts);
    if (parse_result < 0) {
        print_usage(argv[0]);
        return IPC_EXIT_SUCCESS;
    }
    if (parse_result == 0) {
        return IPC_EXIT_INVALID;
    }
    
    /* Initialize IPC client */
    if (ipc_client_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize IPC client\n");
        return IPC_EXIT_ERROR;
    }
    
    print_test_header("Stress");
    
    /* Check if service is running */
    if (!check_service_running()) {
        ipc_client_cleanup();
        return IPC_EXIT_NOT_RUNNING;
    }
    
    printf("Service: RUNNING\n\n");
    
    /* Calculate iterations per thread */
    int total_iterations = opts.rate > 0 ? opts.rate * opts.duration : 10000;
    int iterations_per_thread = total_iterations / opts.connections;
    int target_rate_per_thread = opts.rate > 0 ? opts.rate / opts.connections : 0;
    
    printf("Configuration:\n");
    printf("  Connections:    %d\n", opts.connections);
    printf("  Target rate:    %s\n", opts.rate > 0 ? "unlimited" : "unlimited");
    printf("  Duration:       %d seconds\n", opts.duration);
    printf("  Operation:      %s\n", opts.mixed ? "mixed" : opts.operation);
    printf("  Iterations:     %d per thread\n", iterations_per_thread);
    printf("\nStarting stress test...\n\n");
    
    /* Allocate latency arrays */
    double **all_latencies = malloc(opts.connections * sizeof(double *));
    for (int i = 0; i < opts.connections; i++) {
        all_latencies[i] = malloc(iterations_per_thread * sizeof(double));
    }
    
    bool stop_flag = false;
    ThreadArgs *args = malloc(opts.connections * sizeof(ThreadArgs));
    
    for (int i = 0; i < opts.connections; i++) {
        args[i].thread_id = i;
        args[i].iterations = iterations_per_thread;
        args[i].target_rate = target_rate_per_thread;
        strncpy(args[i].operation, opts.operation, sizeof(args[i].operation) - 1);
        args[i].latencies = all_latencies[i];
        args[i].latency_count = 0;
        args[i].success_count = 0;
        args[i].error_count = 0;
        args[i].mixed = opts.mixed;
        args[i].stop_flag = &stop_flag;
    }
    
    ipc_time_t test_start, test_end;
    ipc_get_time(&test_start);
    
#if NCD_PLATFORM_WINDOWS
    /* Windows: Single-threaded with concurrent connections simulation */
    printf("Running single-threaded stress test (Windows)...\n");
    
    for (int iter = 0; iter < iterations_per_thread && !stop_flag; iter++) {
        for (int conn = 0; conn < opts.connections && !stop_flag; conn++) {
            const char *op = opts.mixed ? 
                (iter % 3 == 0 ? "ping" : (iter % 3 == 1 ? "state" : "version")) : opts.operation;
            
            double latency = do_operation(op, conn, iter);
            if (latency >= 0) {
                args[conn].latencies[args[conn].latency_count++] = latency;
                args[conn].success_count++;
            } else {
                args[conn].error_count++;
            }
        }
        
        if ((iter + 1) % 100 == 0) {
            printf("Progress: %d/%d iterations\r", iter + 1, iterations_per_thread);
            fflush(stdout);
        }
        
        /* Check duration */
        ipc_time_t now;
        ipc_get_time(&now);
        if (ipc_elapsed_ms(&test_start, &now) > opts.duration * 1000) {
            stop_flag = true;
        }
    }
#else
    /* POSIX: Multi-threaded */
    printf("Running multi-threaded stress test...\n");
    
    pthread_t *threads = malloc(opts.connections * sizeof(pthread_t));
    WorkerArgs *worker_args = malloc(opts.connections * sizeof(WorkerArgs));
    
    for (int i = 0; i < opts.connections; i++) {
        worker_args[i].args = &args[i];
        worker_args[i].start_idx = 0;
        worker_args[i].count = iterations_per_thread;
        pthread_create(&threads[i], NULL, worker_thread, &worker_args[i]);
    }
    
    /* Monitor progress */
    for (int sec = 0; sec < opts.duration; sec++) {
        sleep(1);
        
        int total_done = 0;
        for (int i = 0; i < opts.connections; i++) {
            total_done += args[i].success_count + args[i].error_count;
        }
        
        printf("Progress: %d/%d requests (%.0f%%)\r",
               total_done, iterations_per_thread * opts.connections,
               (double)total_done * 100.0 / (iterations_per_thread * opts.connections));
        fflush(stdout);
    }
    
    stop_flag = true;
    
    for (int i = 0; i < opts.connections; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    free(worker_args);
#endif
    
    ipc_get_time(&test_end);
    double total_time_ms = ipc_elapsed_ms(&test_start, &test_end);
    
    /* Aggregate results */
    int total_success = 0;
    int total_errors = 0;
    int total_latencies = 0;
    
    for (int i = 0; i < opts.connections; i++) {
        total_success += args[i].success_count;
        total_errors += args[i].error_count;
        total_latencies += args[i].latency_count;
    }
    
    /* Collect all latencies for percentile calculation */
    double *all_latency = malloc(total_latencies * sizeof(double));
    int idx = 0;
    for (int i = 0; i < opts.connections; i++) {
        for (int j = 0; j < args[i].latency_count; j++) {
            all_latency[idx++] = args[i].latencies[j];
        }
    }
    
    /* Calculate statistics */
    double total_latency = 0;
    double min_latency = 999999.0;
    double max_latency = 0;
    
    for (int i = 0; i < total_latencies; i++) {
        total_latency += all_latency[i];
        if (all_latency[i] < min_latency) min_latency = all_latency[i];
        if (all_latency[i] > max_latency) max_latency = all_latency[i];
    }
    
    double avg_latency = total_latencies > 0 ? total_latency / total_latencies : 0;
    
    /* Sort for percentiles */
    qsort(all_latency, total_latencies, sizeof(double), compare_doubles);
    double p99 = total_latencies > 0 ? all_latency[(int)(total_latencies * 0.99)] : 0;
    double p95 = total_latencies > 0 ? all_latency[(int)(total_latencies * 0.95)] : 0;
    
    double total_requests = total_success + total_errors;
    double req_per_sec = total_time_ms > 0 ? total_requests * 1000.0 / total_time_ms : 0;
    double error_rate = total_requests > 0 ? (double)total_errors / total_requests * 100.0 : 0;
    
    printf("\n\n-------------------------\n");
    printf("Stress Test Results\n\n");
    printf("Duration:         %.2f seconds\n", total_time_ms / 1000.0);
    printf("Total requests:   %.0f\n", total_requests);
    printf("Successful:       %d\n", total_success);
    printf("Errors:           %d\n", total_errors);
    printf("Error rate:       %.2f%%\n", error_rate);
    printf("\nThroughput:\n");
    printf("  Requests/sec:   %.1f\n", req_per_sec);
    printf("\nLatency (ms):\n");
    printf("  Min:            %.3f\n", min_latency);
    printf("  Avg:            %.3f\n", avg_latency);
    printf("  Max:            %.3f\n", max_latency);
    printf("  P95:            %.3f\n", p95);
    printf("  P99:            %.3f\n", p99);
    
    /* Cleanup */
    for (int i = 0; i < opts.connections; i++) {
        free(all_latencies[i]);
    }
    free(all_latencies);
    free(all_latency);
    free(args);
    
    ipc_client_cleanup();
    
    if (total_errors == 0) {
        printf("\nResult: PASS\n");
        return IPC_EXIT_SUCCESS;
    } else if (error_rate < 1.0) {
        printf("\nResult: PARTIAL (error rate < 1%%)\n");
        return IPC_EXIT_ERROR;
    } else {
        printf("\nResult: FAIL (high error rate)\n");
        return IPC_EXIT_ERROR;
    }
}
