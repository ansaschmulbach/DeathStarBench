#!/bin/bash
# Verification tool: runs UniqueIdService and MediaService as two separate
# processes, alternated on one core by the ab_alternator ghOSt scheduler
# (same setup as perf_cross_process.sh), with a third Logger process
# watching a shared-memory event log the two services write to (see
# ../src/shm_log.h) -- every request logs a "<uid|media>_start"/"_end"
# event with its wall-clock timestamp, core, pid, and tid. Once all events
# are in, Logger prints a sorted timeline showing exactly when each request
# ran, on which core, and where the scheduler actually switched cores
# between requests -- an independent, out-of-band check on the alternation
# ab_alternator is supposed to be doing, not just a cycle-count aggregate.
#
# Usage:
#   ./tools/timeline_cross_process.sh [num_requests] [timeline_out_file]
#
# Defaults to 100 requests (small enough to eyeball the whole timeline by
# hand). Trace files are generated fresh each run via tools/gen_*_trace
# (built on the fly into /tmp if not already built).
#
# USE_DISPATCHER (env var, unset by default): same as perf_cross_process.sh
# -- switches each service to ServeWithDispatcher() (see ../src/
# utils_thrift.h), a second CFS-pinned thread per process generating
# synthetic Poisson arrivals and waking the worker via a real futex per
# arrival. UID_DISPATCHER_CPU/MEDIA_DISPATCHER_CPU are auto-picked as two
# DISTINCT CPUs (off $GHOST_CPUS, off each other, off the Logger's CPU, and
# off all of their HT siblings) if unset; THROUGHPUT defaults to 10000
# req/s (override per-service via UID_THROUGHPUT/MEDIA_THROUGHPUT). See
# perf_cross_process.sh's header comment for the full flag list.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/ghost_agent_lib.sh"

SOCIALNET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
UID_BIN="$SOCIALNET_DIR/build/src/UniqueIdService/UniqueIdService"
MEDIA_BIN="$SOCIALNET_DIR/build/src/MediaService/MediaService"
LOGGER_BIN="$SOCIALNET_DIR/build/src/Logger/Logger"
GEN_UID_SRC="$SOCIALNET_DIR/tools/gen_uniqueid_trace.cpp"
GEN_MEDIA_SRC="$SOCIALNET_DIR/tools/gen_media_trace.cpp"

NUM_REQUESTS="${1:-100}"
TIMELINE_OUT="${2:-/tmp/timeline_cross_process.txt}"
GHOST_CPUS="${GHOST_CPUS:-1-2}"  # agent CPU + exactly one worker CPU
QUIET_LOGGING="${QUIET_LOGGING:-1}"  # see ../src/logger.h -- unset to get logs back

for bin in "$UID_BIN" "$MEDIA_BIN" "$LOGGER_BIN"; do
  if [ ! -x "$bin" ]; then
    echo "missing $bin -- build it first: (cd build && make UniqueIdService MediaService Logger)" >&2
    exit 1
  fi
done

echo "=== generating $NUM_REQUESTS-request traces ==="
GEN_UID_BIN=/tmp/gen_uniqueid_trace
GEN_MEDIA_BIN=/tmp/gen_media_trace
if [ ! -x "$GEN_UID_BIN" ]; then
  g++ -std=c++14 -O2 -I "$SOCIALNET_DIR/gen-cpp" "$GEN_UID_SRC" \
    "$SOCIALNET_DIR/gen-cpp/UniqueIdService.cpp" "$SOCIALNET_DIR/gen-cpp/ComposePostService.cpp" \
    "$SOCIALNET_DIR/gen-cpp/social_network_types.cpp" -lthrift -lpthread -o "$GEN_UID_BIN"
fi
if [ ! -x "$GEN_MEDIA_BIN" ]; then
  g++ -std=c++14 -O2 -I "$SOCIALNET_DIR/gen-cpp" "$GEN_MEDIA_SRC" \
    "$SOCIALNET_DIR/gen-cpp/MediaService.cpp" "$SOCIALNET_DIR/gen-cpp/social_network_types.cpp" \
    -lthrift -lpthread -o "$GEN_MEDIA_BIN"
fi
UID_TRACE="$(mktemp /tmp/trace_uid_XXXXXX)"
MEDIA_TRACE="$(mktemp /tmp/trace_media_XXXXXX)"
"$GEN_UID_BIN" "$UID_TRACE" "$NUM_REQUESTS" > /dev/null
"$GEN_MEDIA_BIN" "$MEDIA_TRACE" "$NUM_REQUESTS" > /dev/null

echo "=== launching ab_alternator_agent (--ghost_cpus=$GHOST_CPUS) ==="
ghost_launch_agent "$GHOST_USERSPACE_DIR/bazel-bin/ab_alternator_agent" --ghost_cpus="$GHOST_CPUS"
trap ghost_teardown_agent EXIT

TASKS_FILE="$GHOST_ENCLAVE_DIR/tasks"

# 2 events (start+end) per request per service, plus 1 arrival event per
# request per service if USE_DISPATCHER is set (see dispatcher_cpu.h --
# every synthetic arrival Ingress generates logs its own event). Without
# this, Logger's CollectShmLogEvents() stops as soon as it's seen
# expected_events records TOTAL, and arrival events now compete with
# start/end events for that budget -- observed cutting a 30-request run off
# after only ~20 requests per service instead of 30.
EVENTS_PER_REQUEST=2
if [ -n "${USE_DISPATCHER:-}" ]; then EVENTS_PER_REQUEST=3; fi
EXPECTED_EVENTS=$((NUM_REQUESTS * EVENTS_PER_REQUEST * 2))
SHM_CAPACITY=$((EXPECTED_EVENTS + 100))  # headroom above what's actually needed
SHM_NAME="/timeline_$$"
LOGGER_LOG="$(mktemp /tmp/logger_XXXXXX.log)"

# Pin the Logger off of both the agent CPU and the worker CPU(s) in
# $GHOST_CPUS, and off their HT twins too -- otherwise the normal CFS
# scheduler is free to place this "out-of-band" observer on the very core
# it's supposed to be watching from outside, contending for cache/execution
# resources with the thing being measured. See pick_isolated_cpu in
# ghost_agent_lib.sh.
LOGGER_CPU="$(pick_isolated_cpu "$GHOST_CPUS")"
echo "=== launching Logger (shm=$SHM_NAME, capacity=$SHM_CAPACITY, expecting $EXPECTED_EVENTS events, pinned to cpu $LOGGER_CPU) ==="
taskset -c "$LOGGER_CPU" "$LOGGER_BIN" "$SHM_NAME" "$SHM_CAPACITY" "$EXPECTED_EVENTS" 15 "$TIMELINE_OUT" > "$LOGGER_LOG" 2>&1 &
LOGGER_SUDO_PID=$!  # not actually sudo'd, but keep the name consistent with the other scripts
# Wait for Logger's "ready" line -- it must own the shm region before either
# service's MaybeOpenShmLog() call, or that service silently logs nothing.
waited=0
until grep -q "\[Logger\] ready" "$LOGGER_LOG" 2>/dev/null; do
  if [ ! -d "/proc/$LOGGER_SUDO_PID" ]; then
    echo "Logger exited before becoming ready:" >&2
    cat "$LOGGER_LOG" >&2
    exit 1
  fi
  sleep 0.02
  waited=$((waited + 1))
  if [ "$waited" -gt 250 ]; then  # 5s
    echo "timed out waiting for Logger to become ready" >&2
    cat "$LOGGER_LOG" >&2
    exit 1
  fi
done

UID_LOG="$(mktemp /tmp/uid_XXXXXX.log)"
MEDIA_LOG="$(mktemp /tmp/media_XXXXXX.log)"

ENV_ARGS=(GHOST_ENCLAVE_TASKS="$TASKS_FILE" SHM_LOG_NAME="$SHM_NAME")
if [ -n "$QUIET_LOGGING" ]; then ENV_ARGS+=(QUIET_LOGGING="$QUIET_LOGGING"); fi

UID_DISPATCHER_ARGS=()
MEDIA_DISPATCHER_ARGS=()
if [ -n "${USE_DISPATCHER:-}" ]; then
  THROUGHPUT="${THROUGHPUT:-10000}"
  UID_DISPATCHER_CPU="${UID_DISPATCHER_CPU:-$(pick_isolated_cpu "$GHOST_CPUS,$LOGGER_CPU")}"
  MEDIA_DISPATCHER_CPU="${MEDIA_DISPATCHER_CPU:-$(pick_isolated_cpu "$GHOST_CPUS,$LOGGER_CPU,$UID_DISPATCHER_CPU")}"
  UID_DISPATCHER_ARGS=(USE_DISPATCHER=1 THROUGHPUT="${UID_THROUGHPUT:-$THROUGHPUT}" DISPATCHER_CPU="$UID_DISPATCHER_CPU")
  MEDIA_DISPATCHER_ARGS=(USE_DISPATCHER=1 THROUGHPUT="${MEDIA_THROUGHPUT:-$THROUGHPUT}" DISPATCHER_CPU="$MEDIA_DISPATCHER_CPU")
  echo "=== USE_DISPATCHER enabled: uid dispatcher cpu=$UID_DISPATCHER_CPU throughput=${UID_THROUGHPUT:-$THROUGHPUT}, media dispatcher cpu=$MEDIA_DISPATCHER_CPU throughput=${MEDIA_THROUGHPUT:-$THROUGHPUT} ==="
fi

echo "=== launching UniqueIdService and MediaService, both enrolled in $TASKS_FILE ==="
sudo env "${ENV_ARGS[@]}" "${UID_DISPATCHER_ARGS[@]}" TRACE_FILE="$UID_TRACE" \
  "$UID_BIN" > "$UID_LOG" 2>&1 &
UID_SUDO_PID=$!
sudo env "${ENV_ARGS[@]}" "${MEDIA_DISPATCHER_ARGS[@]}" TRACE_FILE="$MEDIA_TRACE" \
  "$MEDIA_BIN" > "$MEDIA_LOG" 2>&1 &
MEDIA_SUDO_PID=$!

# Restrict each worker to the non-agent cpu(s) -- see perf_cross_process.sh's
# header comment for why. Real child PID, not the sudo monitor -- see
# sudo_child_pid's own comment for why that distinction matters. Best-effort
# race against the process's own startup (no STARTUP_DELAY_MS wait here
# unlike perf_cross_process.sh): on a short/fast run (few requests, no
# dispatcher) the process can exit before sudo_child_pid resolves and
# taskset gets to it, which under `set -e` aborted the whole script when
# taskset's "No such process" failure wasn't tolerated -- `|| true` on each,
# since a task that finishes that fast was never at meaningful risk of a
# stray dispatch onto the wrong cpu anyway.
WORKER_CPUS="$(ghost_worker_cpus "$GHOST_CPUS")"
UID_REAL_PID="$(sudo_child_pid "$UID_SUDO_PID" 2>/dev/null || true)"
MEDIA_REAL_PID="$(sudo_child_pid "$MEDIA_SUDO_PID" 2>/dev/null || true)"
if [ -n "$UID_REAL_PID" ]; then sudo taskset -pc "$WORKER_CPUS" "$UID_REAL_PID" > /dev/null 2>&1 || true; fi
if [ -n "$MEDIA_REAL_PID" ]; then sudo taskset -pc "$WORKER_CPUS" "$MEDIA_REAL_PID" > /dev/null 2>&1 || true; fi

wait "$UID_SUDO_PID"; UID_EXIT=$?
wait "$MEDIA_SUDO_PID"; MEDIA_EXIT=$?
echo "UniqueIdService exited $UID_EXIT, MediaService exited $MEDIA_EXIT"

wait "$LOGGER_SUDO_PID"
echo "=== Logger output ==="
cat "$LOGGER_LOG"
echo ""
echo "(timeline also written to $TIMELINE_OUT)"
