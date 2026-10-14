#include "menu.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int default_bursts[] = {250, 400, 550};
static volatile sig_atomic_t menu_interrupted;

/* Record an idle-menu interruption; active runs use the timer's signal handler. */
static void menu_signal(int number)
{
    menu_interrupted = number;
}

/* Reject signs, whitespace, trailing characters, overflow, and out-of-range values. */
int Input_parse_number(const char *text, int maximum, int *value)
{
    char *end;
    long parsed;
    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return -1;
    }
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < 1 || parsed > maximum) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

/* Show both the interactive entry point and the supported direct CLI forms. */
void Menu_print_usage(const char *program)
{
    printf("Usage: %s [--verbose] [quantum_ms [burst_ms ...]]\n"
           "       %s --help\n"
           "No arguments: open the interactive menu (verbose OFF).\n"
           "Numeric arguments: run once; --verbose enables detailed events.\n"
           "--verbose alone: run the default simulation once.\n"
           "Durations: 1..60000 ms; at most 32 processes.\n"
           "Defaults: quantum 100 ms; CPU bursts 250, 400, 550 ms.\n",
           program, program);
}

/*
 * Own fresh process/scheduler objects for one run, including comparison runs.
 * Always destroy an initialized scheduler before returning to the menu or CLI.
 */
int Simulation_run(int quantum_ms, const int *bursts, int count, int verbose,
                   SchedulerStats *stats)
{
    Process processes[MAX_PROCESSES];
    Scheduler scheduler;
    int result;
    int interrupted;
    if (bursts == NULL || count < 1 || count > MAX_PROCESSES) {
        fprintf(stderr, "Error: invalid simulation configuration.\n");
        return EXIT_FAILURE;
    }
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
    scheduler.verbose = verbose;
    result = scheduler.run(&scheduler);
    interrupted = scheduler.timer.interrupted_signal;
    if (result < 0 && interrupted == 0) {
        perror("Scheduler_run");
    }
    if (Scheduler_destroy(&scheduler) < 0) {
        perror("Scheduler_destroy");
        return interrupted != 0 ? 128 + interrupted : EXIT_FAILURE;
    }
    if (interrupted != 0) {
        fprintf(stderr, "Interrupted by signal %d; children cleaned up.\n", interrupted);
        return 128 + interrupted;
    }
    if (result >= 0) {
        Scheduler_print_summary(&scheduler);
        if (stats != NULL) {
            *stats = scheduler.stats;
        }
    }
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Read one complete line, retry invalid entries, and stop cleanly at EOF or a signal. */
static int read_number(const char *prompt, int maximum, int *value)
{
    char line[128];
    while (!menu_interrupted) {
        fputs(prompt, stdout);
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL) {
            if (menu_interrupted || feof(stdin)) {
                return 0;
            }
            perror("Input");
            return -1;
        }
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int ch;
            /* Discard the rest of an oversized line so it cannot become a menu choice. */
            do {
                ch = getchar();
            } while (ch != '\n' && ch != EOF && !menu_interrupted);
            if (menu_interrupted) {
                return 0;
            }
            fprintf(stderr, "Input too long. Enter an integer from 1 to %d.\n", maximum);
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (Input_parse_number(line, maximum, value) == 0) {
            return 1;
        }
        fprintf(stderr, "Invalid input. Enter an integer from 1 to %d.\n", maximum);
    }
    return 0;
}

/* Render the menu and the verbosity preference retained between simulations. */
static void print_menu(int verbose)
{
    puts("\n================================================");
    puts("             TIME-SHARING SIMULATOR");
    puts("               Round Robin Scheduler");
    puts("================================================\n");
    puts("[1] Run Default Simulation\n[2] Run Custom Simulation\n"
         "[3] Compare Time Quantums\n[4] Toggle Verbose Mode\n"
         "[5] About / Help\n[6] Exit\n");
    puts("------------------------------------------------");
    printf("Verbose Mode: %s\n", verbose ? "ON" : "OFF");
}

/* Explain the OS mechanisms and the limits of a userspace scheduling demonstration. */
static void print_about(void)
{
    puts("\nTime-sharing: give several processes turns to make progress.\n"
         "Round Robin: visit ready processes in cyclic order.\n"
         "Time quantum: maximum wall-clock interval requested for each turn.\n"
         "Parent/children: the parent coordinates separate worker processes.\n"
         "fork(): create each child with its own copy of the parent's memory.\n"
         "SIGSTOP / SIGCONT: stop and resume a worker.\n"
         "setitimer() / SIGALRM: notify the parent when a quantum expires.\n"
         "waitpid(): observe stops and collect exits to avoid zombies.\n"
         "Function pointers: select scheduling methods, worker behavior, and callbacks.\n"
         "This userspace simulator controls worker eligibility; the Linux kernel\n"
         "scheduler still decides when runnable processes execute on a CPU.\n"
         "CPU targets exclude stopped time; elapsed time includes startup and waiting.\n"
         "Dispatches count successful resumes; preemptions count confirmed stops\n"
         "after quantum expiry. Completed means successful exits; failed means\n"
         "nonzero exits or fatal signals. Measurements vary with system load.\n"
         "Custom durations: 1..60000 ms. Ctrl+C cleans up and exits the program.\n");
    Menu_print_usage("./timeshare");
}

/* Run identical workloads with three quanta and print only measured comparison data. */
static int compare_quantums(int verbose)
{
    const int quantums[] = {100, 250, 700};
    SchedulerStats results[3];
    for (int i = 0; i < 3; ++i) {
        int result = Simulation_run(quantums[i], default_bursts, 3, verbose, &results[i]);
        if (result != 0 || menu_interrupted) {
            return menu_interrupted ? 128 + (int)menu_interrupted : result;
        }
    }
    puts("\nQUANTUM COMPARISON (CPU targets: 250, 400, 550 ms)");
    puts("------------------------------------------------------------");
    puts("Quantum (ms)  Dispatches  Preemptions  Elapsed (ms)");
    for (int i = 0; i < 3; ++i) {
        printf("%12d  %10lu  %11lu  %12.3f\n", quantums[i], results[i].dispatches,
               results[i].preemptions, results[i].elapsed_ms);
    }
    puts("Measurements vary with system load and signal delivery overhead.");
    return 0;
}

/* Process menu choices; EOF cancels incomplete input without starting a simulation. */
static int menu_loop(void)
{
    int verbose = 0;
    while (!menu_interrupted) {
        int choice;
        int result;
        print_menu(verbose);
        result = read_number("Select an option: ", 6, &choice);
        if (result <= 0) {
            return result < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
        }
        if (choice == 6) {
            puts("Goodbye.");
            return EXIT_SUCCESS;
        }
        if (choice == 4) {
            verbose = !verbose;
            continue;
        }
        if (choice == 5) {
            print_about();
            continue;
        }
        if (choice == 3) {
            result = compare_quantums(verbose);
        } else {
            int quantum = 100;
            int bursts[3] = {250, 400, 550};
            if (choice == 2) {
                result = read_number("Time quantum (1..60000 ms): ", MAX_DURATION_MS, &quantum);
                if (result <= 0) {
                    return result < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
                }
                for (int i = 0; i < 3; ++i) {
                    char prompt[64];
                    (void)snprintf(prompt, sizeof prompt, "P%d CPU target (1..60000 ms): ", i + 1);
                    result = read_number(prompt, MAX_DURATION_MS, &bursts[i]);
                    if (result <= 0) {
                        return result < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
                    }
                }
            }
            result = Simulation_run(quantum, bursts, 3, verbose, NULL);
        }
        if (result != 0) {
            /* Do not start another run after a cleanup or operating-system failure. */
            return result;
        }
    }
    return 128 + (int)menu_interrupted;
}

/* Keep idle-input interruption safe and restore the caller's original handlers. */
int Menu_run(void)
{
    struct sigaction action = {0};
    struct sigaction original_int;
    struct sigaction original_term;
    int result;
    menu_interrupted = 0;
    action.sa_handler = menu_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &original_int) < 0) {
        perror("Menu SIGINT handler");
        return EXIT_FAILURE;
    }
    if (sigaction(SIGTERM, &action, &original_term) < 0) {
        perror("Menu SIGTERM handler");
        (void)sigaction(SIGINT, &original_int, NULL);
        return EXIT_FAILURE;
    }
    result = menu_loop();
    if (menu_interrupted) {
        result = 128 + (int)menu_interrupted;
        fprintf(stderr, "\nInterrupted by signal %d.\n", (int)menu_interrupted);
    }
    if (sigaction(SIGINT, &original_int, NULL) < 0) {
        perror("Restore SIGINT handler");
        result = EXIT_FAILURE;
    }
    if (sigaction(SIGTERM, &original_term, NULL) < 0) {
        perror("Restore SIGTERM handler");
        result = EXIT_FAILURE;
    }
    return result;
}
