/* test_posix_exit.h -- Safe POSIX process exit status decoding
 * Shared helper for service test files that launch subprocesses.
 * Ensures WIFEXITED is checked before WEXITSTATUS is used.
 */
#ifndef TEST_POSIX_EXIT_H
#define TEST_POSIX_EXIT_H

#ifdef _WIN32
/* Windows: pclose returns the exit code directly (no macro needed) */
#include <stdio.h>
static inline int safe_exit_code(int status) {
    (void)status; /* unused on Windows; exit code is return value of run_command */
    return 0; /* actual exit code comes from the run_command return, not pclose */
}
#else
/* POSIX: use WIFEXITED/WEXITSTATUS macros from sys/wait.h */
#include <sys/wait.h>
#include <stdio.h>

static inline int safe_exit_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    /* Process terminated by signal or didn't exit normally */
    fprintf(stderr, "WARNING: subprocess did not exit normally (status=%d)\n", status);
    return -1;
}
#endif

#endif /* TEST_POSIX_EXIT_H */
