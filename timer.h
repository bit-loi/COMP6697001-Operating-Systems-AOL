
#ifndef TIMER_H
#define TIMER_H

#include <signal.h>

typedef struct Timer Timer;
/* Delivered by Timer_wait in ordinary code, NEVER by the signal handler. */
typedef void (*TimerNotifyFn)(Timer *self, void *context);

/* Owns timer notification state and the signal configuration needed for restoration. */
struct Timer {
    TimerNotifyFn on_expire;       /* Expiry callback invoked by Timer_wait. */
    void *context;                 /* Borrowed; must outlive this timer. */
    sigset_t original_mask;        /* Signal mask saved before initialization. */
    sigset_t wait_mask;            /* Mask used to receive signals while waiting. */
    struct sigaction original_actions[4]; /* Saved handlers for managed signals. */
    int interrupted_signal;       /* Received SIGINT or SIGTERM; zero if none. */
    int initialized;              /* Whether this object owns timer resources. */
};

/* Single-threaded, exclusive ownership of ITIMER_REAL and four signals. */
/* Save signal settings and install the timer's notification handlers. */
int Timer_init(Timer *self, TimerNotifyFn on_expire, void *context);
/* Arm a one-shot timer after clearing any previous expiration. */
int Timer_start(Timer *self, int milliseconds);
/* Disarm the timer and discard stale alarm notifications. */
int Timer_stop(Timer *self);
/* Wait for a managed event and deliver any expiry callback in normal code. */
int Timer_wait(Timer *self);
/* Release timer ownership and restore the original signal configuration. */
int Timer_destroy(Timer *self);

#endif
