#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Select the interactive menu or validate and execute one direct CLI simulation. */
int main(int argc, char **argv)
{
    int bursts[MAX_PROCESSES] = {250, 400, 550};
    int quantum_ms = 100;
    int count = 3;
    int first = 1;
    int verbose = 0;

    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 1) {
        return Menu_run();
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        Menu_print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[first], "--verbose") == 0) {
        verbose = 1;
        ++first;
    }
    if (argc - first > MAX_PROCESSES + 1) {
        fprintf(stderr, "Error: at most %d process CPU targets are allowed.\n", MAX_PROCESSES);
        return EXIT_FAILURE;
    }
    if (first < argc && Input_parse_number(argv[first], MAX_DURATION_MS, &quantum_ms) < 0) {
        fprintf(stderr, "Error: invalid quantum '%s'; expected an integer from 1 to %d ms.\n"
                        "Use --help for command syntax.\n", argv[first], MAX_DURATION_MS);
        return EXIT_FAILURE;
    }
    if (argc > first + 1) {
        count = argc - first - 1;
        for (int i = 0; i < count; ++i) {
            if (Input_parse_number(argv[first + 1 + i], MAX_DURATION_MS, &bursts[i]) < 0) {
                fprintf(stderr, "Error: invalid CPU target for P%d: '%s'; "
                                "expected an integer from 1 to %d ms.\n",
                        i + 1, argv[first + 1 + i], MAX_DURATION_MS);
                return EXIT_FAILURE;
            }
        }
    }
    return Simulation_run(quantum_ms, bursts, count, verbose, NULL);
}
