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

BIN_ABS="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"

echo "=== launching fifo_centralized_agent (--ghost_cpus=$GHOST_CPUS) ==="
ghost_launch_agent "$GHOST_USERSPACE_DIR/bazel-bin/fifo_centralized_agent" --ghost_cpus="$GHOST_CPUS"
trap ghost_teardown_agent EXIT

echo "=== running $(basename "$BIN_ABS") under perf ==="
ENV_ARGS=(GHOST_ENCLAVE_TASKS="$GHOST_ENCLAVE_DIR/tasks")
if [ -n "$TRACE_FILE" ]; then
  ENV_ARGS+=(TRACE_FILE="$(cd "$(dirname "$TRACE_FILE")" && pwd)/$(basename "$TRACE_FILE")")
fi

cd "$(dirname "$BIN_ABS")"
sudo env "${ENV_ARGS[@]}" perf stat \
  -e cycles:u,cycles:k,instructions:u,instructions:k,task-clock,context-switches \
  -o "$PERF_OUT" \
  -- "./$(basename "$BIN_ABS")"

echo "--- perf stats ($PERF_OUT) ---"
cat "$PERF_OUT"
