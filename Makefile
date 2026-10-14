
CC = gcc
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
TARGET = timeshare
OBJECTS = main.o scheduler.o process.o timer.o

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

# Embedded to preserve the nine-file submission. Requires Python 3 on Linux.
define TEST_SCRIPT
import os
import re
import signal
import subprocess
from collections import Counter

def run(args):
    return subprocess.run(["./timeshare", *map(str, args)], text=True,
                          capture_output=True, timeout=20)

def check(args, count):
    result = run(args)
    assert result.returncode == 0, result.stderr + result.stdout
    assert f"DONE completed={count} failed=0" in result.stdout, result.stdout
    exits = re.findall(r"^EXIT P(\d+).*code=0$$", result.stdout, re.M)
    assert Counter(exits) == Counter(map(str, range(1, count + 1))), result.stdout
    finished = set()
    active = None
    for line in result.stdout.splitlines():
        fields = line.split()
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

output = check([5, 40, 40, 40], 3)
assert re.findall(r"^RUN P(\d+)", output, re.M)[:6] == ["1", "2", "3"] * 2, output
assert "PAUSE" in output, output
early = check([60000, 5, 10, 15], 3)
assert "PAUSE" not in early, early
check([2, 15], 1)
check([2, 2, 25, 5], 3)
check([1] + [2] * 32, 32)
check([], 3)
check([10], 3)
for _ in range(10):
    check([1, 2, 3, 4], 3)
for args in ([0], [-1], [60001], ["abc"], ["1x"], [""], [" 1"],
             [1, 0], [1, "9" * 100], [1] + [1] * 33):
    assert run(args).returncode != 0, args
assert run(["--help"]).returncode == 0

for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGKILL):
    process = subprocess.Popen(["./timeshare", "10", "5000", "30"],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True)
    pids = []
    try:
        for line in process.stdout:
            if line.startswith("CREATE"):
                pids.append(int(re.search(r"pid=(\d+)", line).group(1)))
            if line.startswith("RUN"):
                break
        assert len(pids) == 2
        if sig == signal.SIGKILL:
            os.kill(pids[0], sig)  # Abnormal worker exit; parent must keep running.
        else:
            process.send_signal(sig)
        output, error = process.communicate(timeout=5)
        expected = 1 if sig == signal.SIGKILL else 128 + sig
        assert process.returncode == expected, (process.returncode, output, error)
        if sig == signal.SIGKILL:
            assert "DONE completed=2 failed=1" in output, output
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
print("PASS: rotation, early exits, single/32 workers, defaults, stress,")
print("      input validation, SIGINT/SIGTERM cleanup, abnormal child exit")
endef
export TEST_SCRIPT

test: $(TARGET)
	@python3 -c "$$TEST_SCRIPT"

clean:
	rm -f $(TARGET) $(OBJECTS) $(OBJECTS:.o=.d)
