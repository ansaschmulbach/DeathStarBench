#!/bin/bash
# Measures UniqueIdService and MediaService running as two THREADS of the
# SAME process (CombinedService), each self-enrolled into the ghOSt enclave
# as its own task and scheduled by the same ab_alternator agent used for the
# cross-process scenario. This isolates the address-space-switch cost:
# same-process alternation shares one mm/page-table set, so it should NOT
# pay the CR3 reload / TLB-domain switch that perf_cross_process.sh's
# separate-process alternation does -- compare cycles:k between the two to
# see the effect (or lack of one).
#
# perf's --per-thread breakdown (separate counts for the uid thread vs. the
# media thread, not just a combined total) is only available via live attach
# (-p/-t/-a), not via `perf stat -- <command>` wrapping. So this script
# leans on CombinedService's STARTUP_DELAY_MS hook (see MaybeAnnounceAndDelay
# in CombinedService.cpp): each worker thread joins the ghOSt enclave, then
# prints its own TID and sleeps for STARTUP_DELAY_MS before doing any real
# work. That gives a reliable window to read both TIDs off stdout and attach
# perf -t <tid1>,<tid2> --per-thread before the sleep elapses -- avoiding the
# attach-race that plain `perf -p <pid>` (attached after the fact) is
# vulnerable to on a run this short.
#
# Usage:
#   ./tools/perf_same_process.sh [uid_trace] [media_trace] [perf_output_file] [startup_delay_ms]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/ghost_agent_lib.sh"

SOCIALNET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMBINED_BIN="$SOCIALNET_DIR/build/src/CombinedService/CombinedService"

UID_TRACE="${1:-$SOCIALNET_DIR/../../social-network-microservices/trace-unique-id-service}"
MEDIA_TRACE="${2:-$SOCIALNET_DIR/../../social-network-microservices/trace-media-service}"
PERF_OUT="${3:-perf_same_process.txt}"
STARTUP_DELAY_MS="${4:-6000}"
GHOST_CPUS="${GHOST_CPUS:-1-2}"  # agent CPU + exactly one worker CPU

echo "=== launching ab_alternator_agent (--ghost_cpus=$GHOST_CPUS) ==="
ghost_launch_agent "$GHOST_USERSPACE_DIR/bazel-bin/ab_alternator_agent" --ghost_cpus="$GHOST_CPUS"
trap ghost_teardown_agent EXIT

TASKS_FILE="$GHOST_ENCLAVE_DIR/tasks"
COMBINED_LOG="$(mktemp /tmp/combined_XXXXXX.log)"

echo "=== launching CombinedService (STARTUP_DELAY_MS=$STARTUP_DELAY_MS) ==="
( cd "$(dirname "$COMBINED_BIN")" && \
  sudo env GHOST_ENCLAVE_TASKS="$TASKS_FILE" \
    TRACE_FILE_UID="$UID_TRACE" TRACE_FILE_MEDIA="$MEDIA_TRACE" \
    STARTUP_DELAY_MS="$STARTUP_DELAY_MS" \
    "./$(basename "$COMBINED_BIN")" > "$COMBINED_LOG" 2>&1 ) &
COMBINED_SHELL_PID=$!

# Poll the log for both announced TIDs. Each thread prints exactly once,
# right after joining the enclave and before its startup sleep.
UID_TID=""
MEDIA_TID=""
waited=0
while [ -z "$UID_TID" ] || [ -z "$MEDIA_TID" ]; do
  UID_TID="$(grep -oP '(?<=\[startup_delay\] uid tid=)\d+' "$COMBINED_LOG" 2>/dev/null || true)"
  MEDIA_TID="$(grep -oP '(?<=\[startup_delay\] media tid=)\d+' "$COMBINED_LOG" 2>/dev/null || true)"
  sleep 0.05
  waited=$((waited + 1))
  if [ "$waited" -gt 200 ]; then  # 10s
    echo "timed out waiting for both thread TIDs to be announced" >&2
    cat "$COMBINED_LOG" >&2
    exit 1
  fi
done
echo "uid tid=$UID_TID, media tid=$MEDIA_TID"

sudo perf stat -t "$UID_TID,$MEDIA_TID" --per-thread \
  -e cycles:u,cycles:k,instructions:u,instructions:k,task-clock,context-switches \
  -o "$PERF_OUT" &
PERF_PID=$!

# Give perf a moment to actually attach before the announced threads' sleep
# elapses and real work starts.
sleep 0.3

wait "$COMBINED_SHELL_PID"
echo "CombinedService exited"

sudo kill -INT "$PERF_PID" 2>/dev/null || true
wait "$PERF_PID" 2>/dev/null || true

echo "--- perf stats ($PERF_OUT), per-thread ---"
cat "$PERF_OUT"
echo "--- CombinedService log ($COMBINED_LOG) ---"; cat "$COMBINED_LOG"
