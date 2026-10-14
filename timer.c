
#include "timer.h"

#include <errno.h>
#include <stddef.h>
#include <sys/time.h>
#include <time.h>

static const int managed_signals[] = {SIGALRM, SIGCHLD, SIGINT, SIGTERM};
static volatile sig_atomic_t alarm_pending;
static volatile sig_atomic_t child_pending;
static volatile sig_atomic_t interrupt_pending;
static int timer_owned;

/* Record alarm, child, or shutdown signals using only atomic flag assignments. */
static void signal_notice(int number)
{
    if (number == SIGALRM) {
        alarm_pending = 1;
    } else if (number == SIGCHLD) {
        child_pending = 1;
    } else {
        interrupt_pending = number;
    }
}

/*
 * Claim the process-wide timer and install handlers for the four managed signals.
 * Save the previous signal configuration and restore it if setup fails.
 */
int Timer_init(Timer *self, TimerNotifyFn on_expire, void *context)
{
    sigset_t blocked;
    sigset_t pending;
    struct itimerval existing;
    struct sigaction action = {0};
    size_t installed = 0;
    int saved_errno;
    if (self == NULL || on_expire == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (timer_owned) {
        errno = EBUSY;
        return -1;
    }
    *self = (Timer){.on_expire = on_expire, .context = context};
    sigemptyset(&blocked);
    for (size_t i = 0; i < 4; ++i) {
        sigaddset(&blocked, managed_signals[i]);
    }
    if (sigprocmask(SIG_BLOCK, &blocked, &self->original_mask) < 0) {
        return -1;
    }
    if (getitimer(ITIMER_REAL, &existing) < 0 || sigpending(&pending) < 0) {
        goto rollback;
    }
    if (existing.it_value.tv_sec != 0 || existing.it_value.tv_usec != 0 ||
        sigismember(&pending, SIGALRM) == 1) {
        errno = EBUSY;
        goto rollback;
    }
    action.sa_handler = signal_notice;
    action.sa_mask = blocked;
    for (; installed < 4; ++installed) {
        if (sigaction(managed_signals[installed], &action,
                      &self->original_actions[installed]) < 0) {
            goto rollback;
        }
    }
    self->wait_mask = self->original_mask;
    for (size_t i = 0; i < 4; ++i) {
        sigdelset(&self->wait_mask, managed_signals[i]);
    }
    alarm_pending = child_pending = interrupt_pending = 0;
    timer_owned = 1;
    self->initialized = 1;
    return 0;

rollback:
    saved_errno = errno;
    while (installed > 0) {
        --installed;
        (void)sigaction(managed_signals[installed],
                        &self->original_actions[installed], NULL);
    }
    (void)sigprocmask(SIG_SETMASK, &self->original_mask, NULL);
    errno = saved_errno;
    return -1;
}

/* Disable the timer and discard pending alarms so they cannot affect a new turn. */
int Timer_stop(Timer *self)
{
    const struct itimerval disabled = {0};
    const struct timespec no_wait = {0};
    sigset_t alarms;
    if (self == NULL || !self->initialized) {
        errno = EINVAL;
        return -1;
    }
    if (setitimer(ITIMER_REAL, &disabled, NULL) < 0) {
        return -1;
    }
    /* SIGALRM is blocked. Discard stale expirations before rearming. */
    sigemptyset(&alarms);
    sigaddset(&alarms, SIGALRM);
    for (;;) {
        if (sigtimedwait(&alarms, NULL, &no_wait) >= 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN) {
            return -1;
        }
        break;
    }
    alarm_pending = 0;
    return 0;
}

/* Clear the previous alarm and arm a one-shot wall-clock timer in milliseconds. */
int Timer_start(Timer *self, int milliseconds)
{
    struct itimerval interval = {0};
    if (milliseconds < 1) {
        errno = EINVAL;
        return -1;
    }
    if (Timer_stop(self) < 0) {
        return -1;
    }
    interval.it_value.tv_sec = milliseconds / 1000;
    interval.it_value.tv_usec = (milliseconds % 1000) * 1000;
    return setitimer(ITIMER_REAL, &interval, NULL);
}

/*
 * Wait for an alarm, child event, or shutdown signal without a lost-wakeup race.
 * Record interruptions and deliver expiry callbacks outside the signal handler.
 */
int Timer_wait(Timer *self)
{
    if (self == NULL || !self->initialized) {
        errno = EINVAL;
        return -1;
    }
    /* Atomically unblock and sleep: no signal can fall between the two. */
    while (!alarm_pending && !child_pending && !interrupt_pending) {
        if (sigsuspend(&self->wait_mask) < 0 && errno != EINTR) {
            return -1;
        }
    }
    child_pending = 0;
    self->interrupted_signal = (int)interrupt_pending;
    if (alarm_pending) {
        alarm_pending = 0;
        self->on_expire(self, self->context);
    }
    return 0;
}

/* Stop the timer, restore saved handlers and the signal mask, and release ownership. */
int Timer_destroy(Timer *self)
{
    int first_error = 0;
    if (self == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!self->initialized) {
        return 0;
    }
    if (Timer_stop(self) < 0) {
        first_error = errno;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (sigaction(managed_signals[i], &self->original_actions[i], NULL) < 0 &&
            first_error == 0) {
            first_error = errno;
        }
    }
    self->initialized = 0;
    timer_owned = 0;
    if (sigprocmask(SIG_SETMASK, &self->original_mask, NULL) < 0 && first_error == 0) {
        first_error = errno;
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}
