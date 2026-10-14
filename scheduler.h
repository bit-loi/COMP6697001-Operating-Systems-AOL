
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"
#include "timer.h"

typedef struct Scheduler Scheduler;
/* Scheduling method: return an eligible process index, or -1 if none remains. */
typedef int (*SelectNextFn)(Scheduler *self, int last_index);
/* Execution method: run the scheduler and return its completion status. */
typedef int (*RunPolicyFn)(Scheduler *self);

/* Measurements from one finished simulation; never reused across runs. */
typedef struct {
    unsigned long dispatches;     /* Sum of all process dispatch counts. */
    unsigned long preemptions;    /* Sum of all confirmed quantum preemptions. */
    int completed;               /* Children that exited successfully. */
    int failed;                  /* Children with nonzero exits or fatal signals. */
    double elapsed_ms;           /* Monotonic wall time from startup through reaping. */
} SchedulerStats;

/* Coordinates child lifetimes, time slices, and replaceable scheduling methods. */
struct Scheduler {
    Process *processes;             /* Borrowed array; owns its child lifetimes. */
    int process_count;             /* Number of entries in the borrowed array. */
    int quantum_ms;                /* Wall-clock duration of each scheduled turn. */
    Timer timer;                   /* Composition: owned by scheduler. */
    int quantum_expired;          /* Set by the timer callback when a turn ends. */
    int initialized;              /* Whether scheduler resources are initialized. */
    int started;                  /* Prevents running the same instance twice. */
    int verbose;                  /* Print detailed events when nonzero. */
    SchedulerStats stats;         /* Actual measurements for this run. */
    SelectNextFn select_next;     /* Method used to choose the next child. */
    RunPolicyFn run;              /* Method used to execute the scheduling policy. */
};

/* Initialize Round Robin methods and the timer using a borrowed process array. */
int Scheduler_init(Scheduler *self, Process *processes, int count, int quantum_ms);
/* Find the next stopped process in cyclic order, or return -1 if none exists. */
int Scheduler_select_next(Scheduler *self, int last_index);
/* Run once; return 0 on success, 1 for worker failures, or -1 on error. */
int Scheduler_run(Scheduler *self);
/* Print per-process counts and aggregate measurements after a finished run. */
void Scheduler_print_summary(const Scheduler *self);
/* Clean up all children and restore the signal settings saved by the timer. */
int Scheduler_destroy(Scheduler *self);

#endif
