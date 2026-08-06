#!/bin/bash
# Measures ONE service (UniqueIdService or MediaService) running alone under
# ghOSt's centralized FIFO scheduler, processing its trace file start to
# finish. This is the "solo baseline" scenario -- no A/B switching, just one
# task getting the CPU whenever it wants it.
#
# Uses `perf stat -- <command>` (perf launches the process itself, from
# exec()) rather than attaching perf to an already-running PID. Attaching
# after the fact races the workload: these traces finish in ~1-3s, and even
# a few hundred ms of attach delay measurably undercounts cycles/task-clock.
# Wrapping is race-free because perf is the one that calls exec().
#
# Usage:
#   ./tools/perf_solo.sh <path/to/UniqueIdService-or-MediaService> [trace_file] [perf_output_file]
#
# Example:
#   ./tools/perf_solo.sh ./build/src/UniqueIdService/UniqueIdService ../../../social-network-microservices/trace-unique-id-service /tmp/solo_uid.txt
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/ghost_agent_lib.sh"

BIN="$1"
TRACE_FILE="${2:-}"
PERF_OUT="${3:-perf_solo.txt}"
GHOST_CPUS="${GHOST_CPUS:-1-2}"  # agent CPU + exactly one worker CPU
QUIET_LOGGING="${QUIET_LOGGING:-1}"  # see ../src/logger.h -- unset to get logs back

BIN_ABS="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"

echo "=== launching fifo_centralized_agent (--ghost_cpus=$GHOST_CPUS) ==="
ghost_launch_agent "$GHOST_USERSPACE_DIR/bazel-bin/fifo_centralized_agent" --ghost_cpus="$GHOST_CPUS"
trap ghost_teardown_agent EXIT

echo "=== running $(basename "$BIN_ABS") under perf ==="
ENV_ARGS=(GHOST_ENCLAVE_TASKS="$GHOST_ENCLAVE_DIR/tasks")
if [ -n "$TRACE_FILE" ]; then
  ENV_ARGS+=(TRACE_FILE="$(cd "$(dirname "$TRACE_FILE")" && pwd)/$(basename "$TRACE_FILE")")
fi
if [ -n "$QUIET_LOGGING" ]; then ENV_ARGS+=(QUIET_LOGGING="$QUIET_LOGGING"); fi

cd "$(dirname "$BIN_ABS")"
# cycles:u/instructions:u deliberately omitted here: the binary itself now
# self-monitors those via perf_event_open (see ../src/perf_counter.h),
# printed at the end of its own run -- more precisely scoped (worker-loop-
# only, excludes setup) than an external attach can be anyway. Asking perf
# for the same hardware events on top of that self-monitoring oversubscribes
# the CPU's limited physical PMU counters; observed in practice to silently
# zero out one of the self-monitored counters under multiplexing. :k stays
# here since self-monitoring is userspace-only (exclude_kernel=1).
sudo env "${ENV_ARGS[@]}" perf stat \
  -e cycles:k,instructions:k,task-clock,context-switches \
  -o "$PERF_OUT" \
  -- "./$(basename "$BIN_ABS")"

echo "--- perf stats ($PERF_OUT) ---"
cat "$PERF_OUT"
