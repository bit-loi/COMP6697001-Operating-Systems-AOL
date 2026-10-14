#ifndef MENU_H
#define MENU_H

#include "scheduler.h"

enum { MAX_PROCESSES = 32, MAX_DURATION_MS = 60000 };

/* Parse decimal digits in 1..maximum, returning 0 on success or -1 on error. */
int Input_parse_number(const char *text, int maximum, int *value);
/* Print command-line syntax and options to stdout. */
void Menu_print_usage(const char *program);
/* Run the interactive menu; each simulation owns a fresh set of objects. */
int Menu_run(void);
/* Run, clean up, and display one simulation; optionally copy out its statistics. */
int Simulation_run(int quantum_ms, const int *bursts, int count, int verbose,
                   SchedulerStats *stats);

#endif
