
#include "process.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * Read a child's status with waitpid and update the parent's Process object.
 * The options control blocking; return 0 on success or -1 on a wait error.
 */
static int collect(Process *self, int options)
{
    int status;
    pid_t result;
    do {
        result = waitpid(self->pid, &status, options);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        return -1;
    }
    if (result > 0) {
        self->wait_status = status;
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            self->state = PROCESS_FINISHED;
        } else if (WIFSTOPPED(status)) {
            self->state = PROCESS_STOPPED;
        }
    }
    return 0;
}

/* Initialize a new process object without creating its OS child yet. */
int Process_init(Process *self, int id, int burst_ms, ProcessWorkerFn worker)
{
    if (self == NULL || id < 1 || burst_ms < 1 || worker == NULL) {
        errno = EINVAL;
        return -1;
    }
    *self = (Process){.id = id, .pid = -1, .burst_ms = burst_ms,
                      .state = PROCESS_NEW, .worker = worker};
    return 0;
}

/*
 * Fork a worker, restore its signal configuration, and wait for its initial stop.
 * The child runs its worker callback after the scheduler first resumes it.
 */
int Process_start(Process *self, const sigset_t *child_mask)
{
    pid_t pid;
    if (self == NULL || self->state != PROCESS_NEW || child_mask == NULL) {
        errno = EINVAL;
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        const int signals[] = {SIGALRM, SIGCHLD, SIGINT, SIGTERM};
        struct sigaction action = {0};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        /* Separate groups keep terminal Ctrl-C directed at the parent. */
        if (setpgid(0, 0) < 0) {
            _exit(125);
        }
        for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) {
            if (sigaction(signals[i], &action, NULL) < 0) {
                _exit(125);
            }
        }
        if (sigprocmask(SIG_SETMASK, child_mask, NULL) < 0) {
            _exit(125);
        }
        self->pid = getpid();
        /* Parent observes this stop before issuing the first CONT. */
        if (raise(SIGSTOP) != 0) {
            _exit(125);
        }
        self->state = PROCESS_RUNNING;
        _exit(self->worker(self) == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    self->pid = pid;
    self->state = PROCESS_RUNNING;
    if (collect(self, WUNTRACED) < 0) {
        return -1;
    }
    if (self->state != PROCESS_STOPPED) {
        errno = ECHILD;
        return -1;
    }
    return 0;
}

/* Send SIGCONT to a stopped child and mark it as running. */
int Process_resume(Process *self)
{
    if (self == NULL || self->state != PROCESS_STOPPED) {
        errno = EINVAL;
        return -1;
    }
    if (kill(self->pid, SIGCONT) < 0) {
        return -1;
    }
    self->state = PROCESS_RUNNING;
    return 0;
}

/* Send SIGSTOP and wait until the child is confirmed stopped or has exited. */
int Process_pause(Process *self)
{
    if (self == NULL || self->state != PROCESS_RUNNING) {
        errno = EINVAL;
        return -1;
    }
    if (kill(self->pid, SIGSTOP) < 0 && errno != ESRCH) {
        return -1;
    }
    /* Exit can race with STOP. collect() accepts either outcome. */
    return collect(self, WUNTRACED);
}

/* Check for a child's stop or exit without blocking when no status is ready. */
int Process_refresh(Process *self)
{
    if (self == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (self->state == PROCESS_NEW || self->state == PROCESS_FINISHED) {
        return 0;
    }
    return collect(self, WNOHANG | WUNTRACED);
}

/* Return true only when the child has been reaped with a zero exit status. */
int Process_succeeded(const Process *self)
{
    return self != NULL && self->state == PROCESS_FINISHED &&
           WIFEXITED(self->wait_status) && WEXITSTATUS(self->wait_status) == 0;
}

/* Kill and reap an unfinished child; new or finished objects need no cleanup. */
int Process_destroy(Process *self)
{
    if (self == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (self->state == PROCESS_NEW || self->state == PROCESS_FINISHED) {
        return 0;
    }
    /* SIGKILL terminates stopped children too; always reap after signaling. */
    if (kill(self->pid, SIGKILL) < 0 && errno != ESRCH) {
        return -1;
    }
    return collect(self, 0);
}

/*
 * Perform arithmetic until the requested amount of process CPU time has elapsed.
 * Time spent stopped does not count toward this worker's CPU burst.
 */
int Process_cpu_worker(Process *self)
{
    struct timespec start;
    struct timespec now;
    volatile uint64_t checksum = (uint64_t)self->id;
    int64_t elapsed_ns;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start) < 0) {
        return -1;
    }
    do {
        /* Observable unsigned arithmetic cannot be optimized into a sleep. */
        for (int i = 0; i < 10000; ++i) {
            checksum = checksum * UINT64_C(1664525) + UINT64_C(1013904223);
        }
        if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) < 0) {
            return -1;
        }
        elapsed_ns = ((int64_t)now.tv_sec - (int64_t)start.tv_sec) *
                     INT64_C(1000000000) + now.tv_nsec - start.tv_nsec;
    } while (elapsed_ns < (int64_t)self->burst_ms * INT64_C(1000000));
    return 0;
}
