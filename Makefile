
CC = gcc
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
TARGET = timeshare
OBJECTS = main.o menu.o scheduler.o process.o timer.o

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

# Embedded to keep the submission source-file count small. Requires Python 3.
define TEST_SCRIPT
import os
import re
import signal
import subprocess
from collections import Counter

def run(args, stdin=None, timeout=30):
    return subprocess.run(["./timeshare", *map(str, args)], text=True,
                          input=stdin, capture_output=True, timeout=timeout)

def summary(output):
    completed = re.search(r"^Completed\s+:\s+(\d+)$$", output, re.M)
    failed = re.search(r"^Failed\s+:\s+(\d+)$$", output, re.M)
    assert completed and failed, output
    return int(completed.group(1)), int(failed.group(1))

def concise_check(args, count):
    result = run(args)
    assert result.returncode == 0, result.stderr + result.stdout
    completed, failed = summary(result.stdout)
    assert completed == count and failed == 0, result.stdout
    assert "ROUND ROBIN SIMULATION RESULT" in result.stdout, result.stdout
    assert not re.search(r"^(CREATE|RUN|PAUSE|EXIT) ", result.stdout, re.M), result.stdout
    return result.stdout

def verbose_check(args, count):
    result = run(["--verbose", *args])
    assert result.returncode == 0, result.stderr + result.stdout
    completed, failed = summary(result.stdout)
    assert completed == count and failed == 0, result.stdout
    exits = re.findall(r"^EXIT P(\d+).*code=0$$", result.stdout, re.M)
    assert Counter(exits) == Counter(map(str, range(1, count + 1))), result.stdout
    finished = set()
    active = None
    for line in result.stdout.splitlines():
        fields = line.split()
        if not fields or fields[0] not in ("CREATE", "RUN", "PAUSE", "EXIT"):
            continue
        if fields[0] == "EXIT":
            finished.add(fields[1])
            if active == fields[1]:
                active = None
        elif fields[0] == "PAUSE":
            assert active == fields[1], result.stdout
            active = None
        elif fields[0] == "RUN":
            assert active is None and fields[1] not in finished, result.stdout
            active = fields[1]
    for pid in re.findall(r"^CREATE .*pid=(\d+)", result.stdout, re.M):
        assert not os.path.exists(f"/proc/{pid}"), f"unreaped child {pid}"
    return result.stdout

# Concise default output is the primary user-facing result.
concise_check([10], 3)
concise_check([100, 250, 400, 550], 3)

# Verbose event sequences and Round Robin rotation.
output = verbose_check([5, 40, 40, 40], 3)
assert re.findall(r"^RUN P(\d+)", output, re.M)[:6] == ["1", "2", "3"] * 2, output
assert "PAUSE" in output, output
early = verbose_check([60000, 5, 10, 15], 3)
assert "PAUSE" not in early, early
verbose_check([2, 15], 1)
verbose_check([2, 2, 25, 5], 3)
verbose_check([1] + [2] * 32, 32)

# Repeated sequential runs must reset timer, signal, and child state.
for _ in range(10):
    concise_check([1, 2, 3, 4], 3)

# Invalid input is rejected before scheduling.
for args in ([0], [-1], [60001], ["abc"], ["1x"], [""], [" 1"],
             [1, 0], [1, "9" * 100], [1] + [1] * 33):
    assert run(args).returncode != 0, args
assert run(["--help"]).returncode == 0

# The interactive menu exits cleanly and can run a simulation from piped input.
menu_exit = run([], stdin="6\n")
assert menu_exit.returncode == 0, menu_exit.stderr
assert "TIME-SHARING SIMULATOR" in menu_exit.stdout, menu_exit.stdout
assert "Goodbye." in menu_exit.stdout, menu_exit.stdout
menu_default = run([], stdin="1\n6\n")
assert menu_default.returncode == 0, menu_default.stderr
assert summary(menu_default.stdout) == (3, 0), menu_default.stdout

# Signal cleanup: SIGINT/SIGTERM clean children; a killed worker is reported.
for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGKILL):
    process = subprocess.Popen(["./timeshare", "--verbose", "10", "5000", "30"],
                               stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True)
    pids = []
    try:
        for line in process.stdout:
            if line.startswith("CREATE"):
                pids.append(int(re.search(r"pid=(\d+)", line).group(1)))
            if line.startswith("RUN"):
                break
        assert len(pids) == 2, pids
        if sig == signal.SIGKILL:
            os.kill(pids[0], sig)  # Abnormal worker exit; parent must keep running.
        else:
            process.send_signal(sig)
        output, error = process.communicate(timeout=5)
        if sig == signal.SIGKILL:
            assert process.returncode == 1, (process.returncode, output, error)
            assert re.search(r"^Failed\s+:\s+1$$", output, re.M), output
        else:
            assert process.returncode == 128 + sig, (process.returncode, output, error)
        assert all(not os.path.exists(f"/proc/{pid}") for pid in pids), pids
    finally:
        if process.poll() is None:
            # Give the parent's cleanup path a chance even after a test error.
            process.terminate()
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                for pid in pids:
                    try:
                        os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                process.kill()
                process.wait()
print("PASS: concise summary, verbose rotation, early exits, single/32 workers,")
print("      defaults, stress, input validation, menu exit/run,")
print("      SIGINT/SIGTERM cleanup, abnormal child exit")
endef
export TEST_SCRIPT

test: $(TARGET)
	@python3 -c "$$TEST_SCRIPT"

clean:
	rm -f $(TARGET) $(OBJECTS) $(OBJECTS:.o=.d)
