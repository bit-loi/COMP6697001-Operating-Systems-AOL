
#ifndef PROCESS_H
#define PROCESS_H

#include <signal.h>
#include <sys/types.h>

typedef struct Process Process;
/* Ordinary callback, executed in the forked child, never a signal handler. */
typedef int (*ProcessWorkerFn)(Process *self);

/* Lifecycle states tracked by the parent using signals and waitpid results. */
typedef enum {
    PROCESS_NEW,
    PROCESS_STOPPED,
    PROCESS_RUNNING,
    PROCESS_FINISHED
} ProcessState;

/* A child process's identity, workload, observed state, and worker behavior. */
struct Process {
    int id;                       /* Display identifier, such as P1 or P2. */
    pid_t pid;                    /* OS process ID; -1 before the child is created. */
    int burst_ms;                 /* CPU time requested by the default worker. */
    ProcessState state;           /* Lifecycle state known to this object. */
    int wait_status;              /* Most recent raw status returned by waitpid. */
    unsigned long dispatches;     /* Successful scheduler-issued SIGCONT requests. */
    unsigned long preemptions;    /* Quantum expirations followed by a confirmed stop. */
    ProcessWorkerFn worker;       /* Callback executed in the child process. */
};

/* Fields are readable for presentation; mutate lifecycle state via this API. */
/* Initialize metadata and worker behavior without forking. */
int Process_init(Process *self, int id, int burst_ms, ProcessWorkerFn worker);
/* Create the child and wait for its initial stop before scheduling begins. */
int Process_start(Process *self, const sigset_t *child_mask);
/* Continue a stopped child with SIGCONT. */
int Process_resume(Process *self);
/* Stop a running child and collect its stop or exit status. */
int Process_pause(Process *self);
/* Update the child's state without waiting for a new event. */
int Process_refresh(Process *self);
/* Test whether the child finished normally with exit code zero. */
int Process_succeeded(const Process *self);
/* Terminate and reap any child that has not already finished. */
int Process_destroy(Process *self);
/* Default worker: consume the configured burst of process CPU time. */
int Process_cpu_worker(Process *self);

#endif
