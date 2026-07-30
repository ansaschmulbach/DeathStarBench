#!/bin/bash
# Runs UniqueIdService or MediaService under `perf stat -p <pid>`, scoped to
# just that one task rather than the whole CPU (`perf stat -C <n>`) -- this
# is the exact methodology used to produce this branch's IPC/cycle-count
# numbers. Scoping to the task, not the CPU, matters a lot on a ghOSt
# kernel: the per-CPU agent thread busy-polls on the same core, and a
# CPU-wide `perf stat -C` measurement counts that spin time as if it were
# the workload's own cycles. `-p <pid>` only counts cycles while that
# specific task is actually on-cpu.
#
# We can't just do `perf stat -- ./Binary`, because perf's own attach has
# to happen after the binary's PID exists but before it's done running --
# with GHOST_SKIP_YIELD unset and a small trace, a solo run can finish in
# well under a second. So: background the binary, grab its PID via $!,
# attach perf to that PID immediately, then wait for the binary to exit
# and signal perf to flush its final counts.
#
# Env vars (all optional, same ones the binaries themselves read):
#   GHOST_ENCLAVE_TASKS  path to a ghOSt enclave's tasks file -- if set, the
#                        binary self-enrolls into it at startup (see
#                        MaybeJoinGhostEnclave() in src/utils.h). Leave
#                        unset on a non-ghOSt kernel; it's a no-op there and
#                        the binary just runs under the normal scheduler.
#   GHOST_SKIP_YIELD     if set, skips the per-request sched_yield() call
#                        (see TFileServer::serve() in src/utils_thrift.h).
#                        Compare with/without to measure its overhead.
#   TRACE_FILE           input trace file (see tools/gen_*_trace.cpp).
#                        Defaults to trace-unique-id-service /
#                        trace-media-service, matching what the generators
#                        produce by default.
#
# Usage: run_with_perf.sh <path/to/UniqueIdService-or-MediaService> [perf_output_file]
#
# Example -- the exact yield vs. no-yield A/B from this branch's commits:
#   ./tools/run_with_perf.sh ./build/src/UniqueIdService/UniqueIdService /tmp/yield.txt
#   GHOST_SKIP_YIELD=1 ./tools/run_with_perf.sh ./build/src/UniqueIdService/UniqueIdService /tmp/noyield.txt
set -e
BIN="$1"
PERF_OUT="${2:-perf_stat.txt}"
BIN_ABS="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
LOG="$(basename "$BIN").run.log"

cd "$(dirname "$BIN_ABS")"
"./$(basename "$BIN_ABS")" > "$LOG" 2>&1 &
PID=$!
echo "$(basename "$BIN_ABS") PID=$PID (log: $(pwd)/$LOG)"

sudo perf stat -p "$PID" -e cycles,instructions,task-clock,context-switches -o "$PERF_OUT" &
PERF_PID=$!

wait "$PID"
EXITCODE=$?
echo "$(basename "$BIN_ABS") exited with code $EXITCODE"

# perf usually notices the tracked pid exit and stops on its own; this kill
# is a backstop and normally races a process perf already reaped, so a
# "No such process" here is expected, not an error.
sudo kill -INT "$PERF_PID" 2>/dev/null || true
wait "$PERF_PID" 2>/dev/null || true

echo "--- perf stats ($PERF_OUT) ---"
cat "$PERF_OUT"
