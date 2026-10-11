# Time Sharing Simulator

A C project for simulating a time-sharing system using processes, signals, and timers. The planned design uses Round Robin scheduling to give each child process a turn to run for a fixed time quantum.

Developed for the **COMP6697001 — Operating Systems** AOL Case Study at **BINUS University**, academic year **2026–2027, Semester 1**.

## Project Status

The project currently contains the initial file structure. Source files, header files, and the Makefile are placeholders; the simulator is not yet implemented or runnable. The design and features below describe the intended implementation.

## Overview

Time sharing allows multiple processes to make progress by taking turns using the CPU. This project aims to demonstrate that concept through a parent process that coordinates the execution of child processes.

The parent will act as the scheduler, using a timer to determine when a time quantum ends and signals to suspend or resume child processes. This is a user-space scheduling demonstration; the host operating system remains responsible for actual CPU scheduling.

## Planned Features

- Create and manage multiple child processes.
- Schedule child processes in Round Robin order.
- Use a timer to control the duration of each turn.
- Use signals to suspend and resume process execution.
- Display scheduling events so process execution can be observed.
- Detect completed processes and release process resources before exiting.

## Scheduling Flow

1. Initialize the scheduler and create the child processes.
2. Keep child processes paused until they receive a turn.
3. Resume the next process in the scheduling queue.
4. Allow it to run until its time quantum expires or it finishes.
5. Pause it if it is still running, then advance to the next process.
6. Repeat until every child process has finished, then clean up and exit.

Completed processes will be removed from the scheduling rotation.

## Project Structure

| File | Intended responsibility |
| --- | --- |
| `main.c` | Program entry point and coordination of initialization, scheduling, and cleanup. |
| `scheduler.c` | Round Robin scheduling logic. |
| `scheduler.h` | Scheduler interface and related declarations. |
| `process.c` | Child process creation, execution, and lifecycle management. |
| `process.h` | Process management interface and related declarations. |
| `timer.c` | Timer configuration and signal handling. |
| `timer.h` | Timer interface and related declarations. |
| `Makefile` | Build rules for compiling and linking the program. |
| `README.md` | Project overview, design, and development documentation. |

## Development Environment

The intended environment is Linux or another UNIX system with the process, signal, and timer APIs required by the implementation.

Required development tools:

- GCC or another compatible C compiler.
- Make for building the project through the Makefile.
- A terminal for compiling, running, and observing the simulator.

## Build and Run

Build and execution instructions will be added after the source code and Makefile are implemented. There is currently no executable, and the empty Makefile does not define any build targets.

## Planned Validation

- Confirm that child processes receive turns in the expected Round Robin order.
- Check that timer expiration causes the scheduler to advance to the next process.
- Verify that completed processes are skipped in subsequent turns.
- Check that the scheduler exits cleanly after all child processes finish.
- Observe how changing the time quantum affects the frequency of scheduling events.

## Coursework Deliverables

The assignment brief specifies the following submission materials:

- C source code.
- The compiled executable.
- A **5–10 minute demonstration video** showing the program running and explaining how the code works.

The demonstration should explain the development environment, process management, scheduling logic, and the roles of signals and timers.
