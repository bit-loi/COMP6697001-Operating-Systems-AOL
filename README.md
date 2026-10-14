# Time-Sharing Simulator: Object-Oriented C11

A working Round Robin time-sharing demonstration for **COMP6697001 - Operating
Systems**, BINUS University, academic year **2026-2027**. A parent scheduler
controls genuine Linux child processes using `fork()`, `SIGSTOP`, `SIGCONT`,
`setitimer()`, `SIGALRM`, and `waitpid()`.

The source is C11 and builds with GCC as C. Linux/POSIX supplies the operating
system APIs; those APIs are not part of the ISO C standard library. No C++
compiler, syntax, runtime, or standard library is used.

## Build and run

Use Linux or WSL with GCC and Make. Python 3 is needed only for automated tests.
Native Windows GCC does not provide the required Linux process APIs.

### Step by step on Windows with WSL

1. Open PowerShell or the VS Code terminal and enter WSL:

   ```powershell
   wsl
   ```

2. Inside the WSL terminal, open this project directory. Windows drive `D:` is
   available as `/mnt/d` in WSL. Adjust the path if you move the project:

   ```sh
   cd /mnt/d/Documents/Code/Binus/OS
   ```

3. Compile the source files:

   ```sh
   make
   ```

   This creates the `timeshare` executable, plus `.o` object files and `.d`
   dependency files. Run `make` again after editing the source code.

4. Run the program with the default settings:

   ```sh
   ./timeshare
   ```

   The default quantum is 100 ms, with three workers requesting 250, 400, and
   550 ms of CPU time. To choose your own settings, try:

   ```sh
   ./timeshare 50 120 250 80
   ```

   Here, `50` is the quantum in milliseconds; `120`, `250`, and `80` are the
   CPU bursts for the three workers. Watch `RUN`, `PAUSE`, and `EXIT` events
   show their turns. A successful run ends with `DONE completed=3 failed=0`.
   Process IDs and the number of turns can vary. Press Ctrl-C to stop a run.

5. Optionally run the automated tests (requires Python 3):

   ```sh
   make test
   ```

6. When finished, remove the generated executable, object files, and dependency
   files:

   ```sh
   make clean
   ```

   The source files remain available. Run `make` before trying the program again.
   To leave WSL and return to PowerShell, type `exit`.

If `make` or `gcc` is missing, install the build tools inside Ubuntu/Debian WSL,
then repeat step 3:

```sh
sudo apt update
sudo apt install build-essential python3
```

If `./timeshare` reports `No such file or directory`, check that you are in the
project directory and that `make` completed successfully.

### Build results are ignored by Git

`.gitignore` excludes everything `make` produces, so `git status` stays clean
after compiling:

```gitignore
timeshare
*.o
*.d
```

Only the source files (`*.c`, `*.h`, `Makefile`, `README.md`, `.gitignore`) are
tracked. Delete the build results with `make clean` if you want the working
tree to be completely empty of generated files.

### Run directly from PowerShell

From PowerShell in this repository, run the Linux commands through WSL:

```powershell
wsl --exec sh -lc 'cd /mnt/d/Documents/Code/Binus/OS && make && make test'
wsl --exec sh -lc 'cd /mnt/d/Documents/Code/Binus/OS && ./timeshare 50 120 250 80'
```

### Compile without Make

Inside Linux or WSL, equivalent direct compilation is:

```sh
gcc -D_POSIX_C_SOURCE=200809L -std=c11 -O2 -g \
    -Wall -Wextra -Wpedantic -Werror \
    main.c scheduler.c process.c timer.c -o timeshare
```

The feature-test macro exposes the POSIX declarations in the system headers.
The Makefile tracks header dependencies with GCC's `-MMD -MP` options.

## Command line and output

```text
./timeshare [quantum_ms [burst_ms ...]]
```

- No arguments: 100 ms quantum and three workers needing 250, 400, and 550 ms of CPU time.
- Only a quantum: retain those three default workers.
- Quantum followed by bursts: create one worker per burst, up to 32 workers.
- Durations must be integers from 1 through 60000 ms. Invalid input exits before forking.
- `--help` prints usage. Ctrl-C or SIGTERM requests cleanup and termination.

Example output shape (PIDs and the number of turns vary):

```text
Round Robin: processes=3 quantum_ms=50
CREATE P1 pid=1234 burst_ms=120 state=STOPPED
CREATE P2 pid=1235 burst_ms=250 state=STOPPED
CREATE P3 pid=1236 burst_ms=80 state=STOPPED
RUN P1 pid=1234
PAUSE P1 reason=quantum
RUN P2 pid=1235
PAUSE P2 reason=quantum
RUN P3 pid=1236
...
EXIT P2 pid=1235 code=0
DONE completed=3 failed=0
```

`CREATE` confirms that the parent observed the initial stop. `RUN` records a
dispatch attempt. `PAUSE` confirms that `waitpid()` observed a stop after quantum
expiration. `EXIT` records a reaped child, including its exit code or terminating
signal. `DONE` reports reaped workers and failures; a failed worker is counted
as completed but also increments `failed`.

Exit status is 0 when every worker succeeds, 1 for invalid input, operating
system errors, or failed workers, 130 for handled SIGINT, and 143 for handled
SIGTERM. Errors go to stderr; scheduling events go to stdout.

## Nine-file architecture

| File | Responsibility |
| --- | --- |
| `main.c` | Validate arguments, allocate objects on the stack, invoke the scheduler method, clean up, choose exit status. |
| `scheduler.h` | Scheduler state, method pointer types, public lifecycle API. |
| `scheduler.c` | Round Robin selection, dispatch, preemption, completion reporting, child ownership. |
| `process.h` | Process state, worker callback type, public lifecycle API. |
| `process.c` | Fork/start handshake, stop/resume/reap operations, CPU worker, child cleanup. |
| `timer.h` | Timer state, notification callback type, public timer API. |
| `timer.c` | Signal registration, masks, one-shot real-time timer, deferred notification, restoration. |
| `Makefile` | Strict C11 GCC build, dependency tracking, embedded integration tests, cleanup. |
| `README.md` | Usage, architecture, synchronization, validation, presentation guide. |

The dependency direction is `main -> scheduler -> process + timer`. Neither
process nor timer includes the scheduler header. The timer's borrowed `void *`
context allows notification without a circular dependency.

## How C emulates object-oriented design

### Objects, methods, and constructors

Each struct combines state belonging to one concept. `Process` holds a PID,
burst duration, lifecycle state, wait status, and worker behavior. `Timer` holds
its callback, saved signal configuration, and interruption state. `Scheduler`
holds process references, a composed timer, and scheduling methods.

An explicit `self` pointer has the role of C++ `this`. `Process_init`,
`Timer_init`, and `Scheduler_init` act as constructors; the matching `destroy`
functions act as destructors. C does not call destructors automatically, so
`main` explicitly calls `Scheduler_destroy` after both successful and failed
runs. Helper functions are `static`, and public declarations live in headers.
Struct fields remain visible to permit stack allocation and an understandable
presentation; callers should change lifecycle state only through the API. This
is API-level encapsulation, not C++ access control.

### Useful function pointers and polymorphism

The function pointers are executed on the normal application path:

```c
/* main.c: dynamically dispatch the scheduling policy. */
result = scheduler.run(&scheduler);

/* scheduler.c: choose the next eligible worker through a method pointer. */
next = self->select_next(self, last_index);

/* process.c: each forked child executes its configured behavior. */
_exit(self->worker(self) == 0 ? EXIT_SUCCESS : EXIT_FAILURE);

/* timer.c: notify the owning scheduler in normal execution context. */
self->on_expire(self, self->context);
```

`SelectNextFn` and `RunPolicyFn` provide a small strategy interface: selection
and the execution policy can be replaced with functions having the same
signatures. Round Robin is the supplied policy. A different selector must
return a stopped process index, or -1 when none remain, and preserve the
one-running-child invariant. A replacement run policy must honor the same
ownership and cleanup rules. The scheduler separates policy decisions from
process-control operations rather than repeating those operations for every
policy.

`ProcessWorkerFn` lets individual workers do different work without changing
forking, scheduling, or reaping. Pass a different worker to `Process_init`;
return 0 for success or nonzero for failure. A callback runs in a separate
address space after `fork()`, so changing its `Process` copy does not update
the parent's copy. Parent state is updated using `waitpid()`.

`TimerNotifyFn` decouples notification from the scheduler. Its context points
to the owning scheduler. `quantum_finished` simply marks the current quantum
expired. Direct typed method fields provide the needed polymorphism; an
additional virtual-table hierarchy would add indirection without helping this
small application. Composition is used instead of inheritance.

### Ownership and lifetime

`main` owns a fixed stack array of at most 32 processes and a stack scheduler.
The scheduler borrows the array, owns the child lifetimes, and contains its
timer by value. The timer borrows its callback context. No heap allocation is
needed. The array and scheduler must remain alive and at stable addresses
until destruction; do not copy or reinitialize a live object.

Initialization must succeed before use. `Scheduler_run` is single-use; create
or reinitialize a destroyed scheduler for another run. Destruction after
successful initialization is safe to repeat. Initialization functions return
0 or -1 with `errno`; a failed timer initialization rolls back its signal
configuration. Process/timer operations return 0 or -1. `Scheduler_run` returns
0 for all-success, 1 for completed workers with failures, or -1 for an OS/API
error. On any run result, the caller must destroy the scheduler.

## Round Robin and real operating system behavior

1. The timer saves the parent's signal mask and dispositions, then blocks its
   managed signals during ordinary parent execution.
2. Each `Process_start` forks a child. The child resets inherited handlers,
   restores the original mask, enters its own process group, and stops itself
   with `SIGSTOP`. The parent waits with `waitpid(..., WUNTRACED)` to confirm
   the stop before continuing initialization. This prevents a first `SIGCONT`
   from arriving before the child has stopped.
3. Starting after the previous index, `select_next` scans cyclically for a
   stopped worker. Finished workers are skipped without moving array elements.
4. The scheduler arms a one-shot `ITIMER_REAL` timer and sends `SIGCONT` to the
   selected child. At most one worker is intentionally runnable at a time.
5. `Timer_wait` uses `sigsuspend` to atomically unblock signals and sleep.
   `SIGALRM` means the wall-clock quantum expired; `SIGCHLD` means child state
   may have changed. On return, normal code checks all children with
   `waitpid(..., WNOHANG | WUNTRACED)` because standard signals may coalesce.
6. An early child exit ends the turn immediately. At quantum expiration, the
   scheduler disarms the timer, sends `SIGSTOP` if the child is still running,
   and waits for stop acknowledgement before dispatching another child. If
   exit races with the stop request, the terminal wait status wins.
7. Completion statuses are reaped once. When no eligible workers remain, the
   scheduler reports the result and the caller destroys it.

Process states are `NEW -> STOPPED -> RUNNING`, with repeated
`RUNNING -> STOPPED -> RUNNING` transitions and eventually `FINISHED`.
Unexpected termination can move any live child to `FINISHED`.

### Signal handlers versus ordinary callbacks

`sigaction` registers `signal_notice` as the actual asynchronous handler. It
only assigns `volatile sig_atomic_t` flags. It does not call user callbacks,
print, allocate, manipulate objects, schedule work, or call `waitpid()`.

In contrast, the worker, scheduler methods, and timer notification are ordinary
function-pointer calls. `Timer_wait` invokes the timer notification only after
`sigsuspend` returns and the managed signals are blocked again. This distinction
is what makes the callback design safe.

Signals remain blocked while the scheduler inspects and changes state.
`sigsuspend` atomically swaps the mask and waits, closing the check-then-sleep
lost-wakeup window. A child-state notification is a hint to inspect all
children, not a count of exits. Before every new quantum, `Timer_stop` disarms
the previous timer and drains pending `SIGALRM` with a zero-timeout
`sigtimedwait`, preventing a stale expiration from shortening the next turn.

### Cleanup and scope

On a run error, partial fork failure, SIGINT, or SIGTERM, scheduler destruction
disarms the timer, kills any remaining child with `SIGKILL` (which works even
when stopped), and reaps it. It then restores the original signal handlers and
mask. Each child has its own process group so a terminal Ctrl-C reaches the
parent, which coordinates cleanup. An externally killed child is reported as
a failed worker while the remaining workers continue.

This is a single-threaded standalone application. One timer owns `ITIMER_REAL`
and SIGALRM/SIGCHLD/SIGINT/SIGTERM at a time; initialization rejects a second
live timer, an already armed real-time timer, or a pending alarm. Do not install
competing handlers, reap these children elsewhere, or externally resume paused
workers. As with other user-space programs, forcibly killing the parent with
SIGKILL cannot execute its cleanup code.

The worker performs unsigned arithmetic and measures its own CPU time with
`CLOCK_PROCESS_CPUTIME_ID`. Time spent stopped does not count toward its burst.
The scheduling timer measures wall time, so a loaded host can require more
turns than `ceil(burst / quantum)`. The host Linux scheduler still controls
physical CPU execution. Signal delivery, printing, and context switches add
overhead; this is a scheduling demonstration, not a hard real-time guarantee.

## Validation

`make test` embeds its Python standard-library integration tests in the
Makefile so the submission keeps exactly nine source/documentation files. It
runs the actual compiled program, checks event sequences and return codes,
and verifies reported child PIDs disappear from Linux `/proc` after exit.

The tests cover:

- The first two Round Robin rotations, plus dispatch/pause/exit ordering.
- Bursts shorter than the quantum and exclusion of completed workers.
- One worker, unequal bursts, and the 32-worker limit.
- Default arguments, quantum-only arguments, and repeated 1 ms quanta.
- Invalid, negative, zero, oversized, malformed, and overflowing arguments.
- SIGINT and SIGTERM cleanup, including reaping stopped workers.
- An externally SIGKILLed child, continued scheduling, and failure reporting.

Manual demonstration commands:

```sh
./timeshare 100 250 400 550  # Clear rotation and several preemptions.
./timeshare 1000 10 20 30   # Each worker exits before a full quantum.
./timeshare 5 50 100 150    # Frequent context switches.
./timeshare 50 5000 5000   # Press Ctrl-C; expect cleanup and exit status 130.
./timeshare 0              # Usage error; no children are created.
```

A strict GCC 13.3.0 build and the integration suite were executed successfully
in Linux through WSL during implementation. A separate temporary C harness,
compiled with UndefinedBehaviorSanitizer, also verified signal-mask/handler
restoration, stale-alarm draining, rejection of competing timers, custom
worker/selector callbacks, repeated destruction, and cleanup after an injected
second-fork failure. That one-off harness is not part of `make test`. These
checks validate the included workload and supported shutdown paths, not hard
real-time timing.

## Suggested 5-10 minute presentation

1. Show the nine files and build with GCC in C11 mode.
2. Explain structs, `self`, initialization/destruction, and stack ownership.
3. Point out the four actual function-pointer call sites and explain how
   replacing a compatible function changes behavior.
4. Run a three-worker example and trace Round Robin selection in the output.
5. Explain the initial stop handshake and the SIGCONT/timer/SIGSTOP cycle.
6. Contrast the flag-only signal handler with the deferred timer callback;
   explain `sigsuspend`, `waitpid`, and early child completion.
7. Compare small and large quanta, demonstrate Ctrl-C cleanup, and run tests.

Coursework submission can include these source files, the Linux executable
produced by `make`, and a recorded demonstration of the build and behavior.
