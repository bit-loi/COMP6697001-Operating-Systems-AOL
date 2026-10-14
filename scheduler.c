#include "scheduler.h"

#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>

/* Mark the current quantum as expired; Timer_wait calls this outside signal context. */
static void quantum_finished(Timer *timer, void *context)
{
    Scheduler *self = context;
    (void)timer;
    self->quantum_expired = 1;
}

/* Print a reaped child's exit code or the signal that terminated it. */
static void report_completion(const Process *process)
{
    if (WIFEXITED(process->wait_status)) {
        printf("EXIT P%d pid=%ld code=%d\n", process->id, (long)process->pid,
               WEXITSTATUS(process->wait_status));
    } else {
        printf("EXIT P%d pid=%ld signal=%d\n", process->id, (long)process->pid,
               WTERMSIG(process->wait_status));
    }
}

/* Refresh every child's status without blocking and report newly finished children. */
static int refresh_all(Scheduler *self)
{
    for (int i = 0; i < self->process_count; ++i) {
        Process *process = &self->processes[i];
        ProcessState previous = process->state;
        if (Process_refresh(process) < 0) {
            return -1;
        }
        if (previous != PROCESS_FINISHED && process->state == PROCESS_FINISHED) {
            report_completion(process);
        }
    }
    return 0;
}

/* Borrow the process array, select Round Robin methods, and initialize the owned timer. */
int Scheduler_init(Scheduler *self, Process *processes, int count, int quantum_ms)
{
    if (self == NULL || processes == NULL || count < 1 || quantum_ms < 1) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (processes[i].state != PROCESS_NEW || processes[i].worker == NULL) {
            errno = EINVAL;
            return -1;
        }
    }
    *self = (Scheduler){.processes = processes, .process_count = count,
                        .quantum_ms = quantum_ms,
                        .select_next = Scheduler_select_next, .run = Scheduler_run};
    if (Timer_init(&self->timer, quantum_finished, self) < 0) {
        return -1;
    }
    self->initialized = 1;
    return 0;
}

/* Scan cyclically after last_index; return the next stopped index or -1 if none exists. */
int Scheduler_select_next(Scheduler *self, int last_index)
{
    int index = last_index;
    for (int visited = 0; visited < self->process_count; ++visited) {
        index = (index + 1) % self->process_count;
        if (self->processes[index].state == PROCESS_STOPPED) {
            return index;
        }
    }
    return -1;
}

/*
 * Start all children and give one child at a time a turn until completion.
 * Return 0 for success, 1 for worker failures, or -1 for an error or interruption.
 * The caller must destroy the scheduler after any outcome to release resources.
 */
int Scheduler_run(Scheduler *self)
{
    int last_index = -1;
    int failed = 0;
    if (self == NULL || !self->initialized || self->started || self->select_next == NULL) {
        errno = EINVAL;
        return -1;
    }
    self->started = 1;
    for (int i = 0; i < self->process_count; ++i) {
        Process *process = &self->processes[i];
        if (Process_start(process, &self->timer.original_mask) < 0) {
            return -1;
        }
        printf("CREATE P%d pid=%ld burst_ms=%d state=STOPPED\n",
               process->id, (long)process->pid, process->burst_ms);
    }
    for (;;) {
        int next;
        Process *active;
        if (refresh_all(self) < 0) {
            return -1;
        }
        next = self->select_next(self, last_index);
        if (next < 0) {
            break;
        }
        if (next >= self->process_count) {
            errno = EINVAL;
            return -1;
        }
        active = &self->processes[next];
        self->quantum_expired = 0;
        printf("RUN P%d pid=%ld\n", active->id, (long)active->pid);
        if (Timer_start(&self->timer, self->quantum_ms) < 0 ||
            Process_resume(active) < 0) {
            return -1;
        }
        while (active->state == PROCESS_RUNNING && !self->quantum_expired) {
            if (Timer_wait(&self->timer) < 0) {
                return -1;
            }
            if (self->timer.interrupted_signal != 0) {
                errno = EINTR;
                return -1;
            }
            if (refresh_all(self) < 0) {
                return -1;
            }
        }
        if (Timer_stop(&self->timer) < 0) {
            return -1;
        }
        if (active->state == PROCESS_RUNNING) {
            if (Process_pause(active) < 0) {
                return -1;
            }
            if (active->state == PROCESS_FINISHED) {
                report_completion(active);
            } else {
                printf("PAUSE P%d reason=quantum\n", active->id);
            }
        }
        last_index = next;
    }
    for (int i = 0; i < self->process_count; ++i) {
        if (!Process_succeeded(&self->processes[i])) {
            ++failed;
        }
    }
    printf("DONE completed=%d failed=%d\n", self->process_count, failed);
    return failed == 0 ? 0 : 1;
}

/* Stop scheduling, kill and reap remaining children, and restore the timer's signals. */
int Scheduler_destroy(Scheduler *self)
{
    int first_error = 0;
    if (self == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!self->initialized) {
        return 0;
    }
    if (Timer_stop(&self->timer) < 0) {
        first_error = errno;
    }
    for (int i = 0; i < self->process_count; ++i) {
        if (Process_destroy(&self->processes[i]) < 0 && first_error == 0) {
            first_error = errno;
        }
    }
    if (Timer_destroy(&self->timer) < 0 && first_error == 0) {
        first_error = errno;
    }
    self->initialized = 0;
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}
