#include "scheduler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_PROCESSES = 32, MAX_DURATION_MS = 60000 };

/* Parse a duration in milliseconds; return 0 on success or -1 if invalid. */
static int parse_ms(const char *text, int *value)
{
    char *end = NULL;
    long parsed;
    if (text[0] < '0' || text[0] > '9') {
        return -1;
    }
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < 1 || parsed > MAX_DURATION_MS) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

/* Print the command syntax, duration limits, and default workload to stderr. */
static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [quantum_ms [burst_ms ...]]\n"
                    "Durations: 1..60000 ms; at most 32 processes.\n"
                    "Defaults: quantum 100 ms; CPU bursts 250, 400, 550 ms.\n",
            program);
}

/*
 * Parse arguments, initialize the process and scheduler objects, and run them.
 * Clean up child processes and translate the result into a program exit code.
 */
int main(int argc, char **argv)
{
    Process processes[MAX_PROCESSES];
    Scheduler scheduler;
    int bursts[MAX_PROCESSES] = {250, 400, 550};
    int quantum_ms = 100;
    int count = 3;
    int result;
    int interrupted;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc > MAX_PROCESSES + 2 ||
        (argc > 1 && parse_ms(argv[1], &quantum_ms) < 0)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc > 2) {
        count = argc - 2;
        for (int i = 0; i < count; ++i) {
            if (parse_ms(argv[i + 2], &bursts[i]) < 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
    }
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 0; i < count; ++i) {
        if (Process_init(&processes[i], i + 1, bursts[i], Process_cpu_worker) < 0) {
            perror("Process_init");
            return EXIT_FAILURE;
        }
    }
    if (Scheduler_init(&scheduler, processes, count, quantum_ms) < 0) {
        perror("Scheduler_init");
        return EXIT_FAILURE;
    }
    printf("Round Robin: processes=%d quantum_ms=%d\n", count, quantum_ms);
    result = scheduler.run(&scheduler);
    if (result < 0) {
        perror("Scheduler_run");
    }
    interrupted = scheduler.timer.interrupted_signal;
    if (Scheduler_destroy(&scheduler) < 0) {
        perror("Scheduler_destroy");
        result = -1;
    }
    if (interrupted != 0) {
        fprintf(stderr, "Interrupted by signal %d; children cleaned up.\n", interrupted);
        return 128 + interrupted;
    }
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
