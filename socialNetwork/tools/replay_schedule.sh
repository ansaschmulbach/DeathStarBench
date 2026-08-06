#!/bin/bash
# Captures the exact dispatch schedule from a real ghOSt-scheduled
# cross-process run (UniqueIdService + MediaService alternated by
# ab_alternator, same setup as perf_cross_process.sh / timeline_cross_process.sh),
# then replays that SAME sequence of requests in a single process with no
# threads, no ghOSt, no scheduler, no futex at all (see ../src/ScheduleReplay)
# and times it under perf. The replay still goes through the real Thrift
# transport/protocol/processor stack for both services (same trace files,
# same TProcessor::process() calls TFileServer::serve() makes) -- only the
# scheduling/dispatch machinery is removed, not the request-handling path
# itself. Comparing the two `perf stat` outputs isolates what ghOSt's
# dispatch machinery (agent polling, task hand-off, the cross-process
# address-space switch) actually costs on top of real request processing --
# same request count, same request content, same order, only the
# scheduling/dispatch mechanism differs.
#
# Usage:
#   ./tools/replay_schedule.sh [num_requests] [schedule_out_file]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/ghost_agent_lib.sh"

SOCIALNET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
UID_BIN="$SOCIALNET_DIR/build/src/UniqueIdService/UniqueIdService"
MEDIA_BIN="$SOCIALNET_DIR/build/src/MediaService/MediaService"
SCHEDULE_LOGGER_BIN="$SOCIALNET_DIR/build/src/ScheduleLogger/ScheduleLogger"
REPLAY_BIN="$SOCIALNET_DIR/build/src/ScheduleReplay/ScheduleReplay"
GEN_UID_SRC="$SOCIALNET_DIR/tools/gen_uniqueid_trace.cpp"
GEN_MEDIA_SRC="$SOCIALNET_DIR/tools/gen_media_trace.cpp"

NUM_REQUESTS="${1:-100}"
SCHEDULE_OUT="${2:-/tmp/schedule.txt}"
GHOST_CPUS="${GHOST_CPUS:-1-2}"  # agent CPU + exactly one worker CPU
QUIET_LOGGING="${QUIET_LOGGING:-1}"  # see ../src/logger.h -- unset to get logs back

for bin in "$UID_BIN" "$MEDIA_BIN" "$SCHEDULE_LOGGER_BIN" "$REPLAY_BIN"; do
  if [ ! -x "$bin" ]; then
    echo "missing $bin -- build it first: (cd build && make UniqueIdService MediaService ScheduleLogger ScheduleReplay)" >&2
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
EXPECTED_EVENTS=$((NUM_REQUESTS * 2 * 2))  # start+end, x2 services
SHM_CAPACITY=$((EXPECTED_EVENTS + 100))  # headroom above what's actually needed
SHM_NAME="/schedule_$$"
SCHEDLOG_LOG="$(mktemp /tmp/schedlog_XXXXXX.log)"

# Pin ScheduleLogger off of both the agent CPU and the worker CPU(s) in
# $GHOST_CPUS, and off their HT twins too -- see the matching note in
# timeline_cross_process.sh / pick_isolated_cpu in ghost_agent_lib.sh.
SCHEDLOG_CPU="$(pick_isolated_cpu "$GHOST_CPUS")"
echo "=== launching ScheduleLogger (shm=$SHM_NAME, capacity=$SHM_CAPACITY, expecting $EXPECTED_EVENTS events, pinned to cpu $SCHEDLOG_CPU) ==="
taskset -c "$SCHEDLOG_CPU" "$SCHEDULE_LOGGER_BIN" "$SHM_NAME" "$SHM_CAPACITY" "$EXPECTED_EVENTS" 15 "$SCHEDULE_OUT" > "$SCHEDLOG_LOG" 2>&1 &
SCHEDLOG_PID=$!
waited=0
until grep -q "\[ScheduleLogger\] ready" "$SCHEDLOG_LOG" 2>/dev/null; do
  if [ ! -d "/proc/$SCHEDLOG_PID" ]; then
    echo "ScheduleLogger exited before becoming ready:" >&2
    cat "$SCHEDLOG_LOG" >&2
    exit 1
  fi
  sleep 0.02
  waited=$((waited + 1))
  if [ "$waited" -gt 250 ]; then  # 5s
    echo "timed out waiting for ScheduleLogger to become ready" >&2
    cat "$SCHEDLOG_LOG" >&2
    exit 1
  fi
done

UID_LOG="$(mktemp /tmp/uid_XXXXXX.log)"
MEDIA_LOG="$(mktemp /tmp/media_XXXXXX.log)"

ENV_ARGS=(GHOST_ENCLAVE_TASKS="$TASKS_FILE" SHM_LOG_NAME="$SHM_NAME")
if [ -n "$QUIET_LOGGING" ]; then ENV_ARGS+=(QUIET_LOGGING="$QUIET_LOGGING"); fi
if [ -n "${GHOST_SKIP_YIELD:-}" ]; then ENV_ARGS+=(GHOST_SKIP_YIELD="$GHOST_SKIP_YIELD"); fi

echo "=== launching UniqueIdService and MediaService, both enrolled in $TASKS_FILE ==="
sudo env "${ENV_ARGS[@]}" TRACE_FILE="$UID_TRACE" \
  "$UID_BIN" > "$UID_LOG" 2>&1 &
UID_SUDO_PID=$!
sudo env "${ENV_ARGS[@]}" TRACE_FILE="$MEDIA_TRACE" \
  "$MEDIA_BIN" > "$MEDIA_LOG" 2>&1 &
MEDIA_SUDO_PID=$!

wait "$UID_SUDO_PID"; UID_EXIT=$?
wait "$MEDIA_SUDO_PID"; MEDIA_EXIT=$?
echo "UniqueIdService exited $UID_EXIT, MediaService exited $MEDIA_EXIT"

wait "$SCHEDLOG_PID"
DISPATCH_COUNT="$(grep -oP '(?<=dumped )\d+' "$SCHEDLOG_LOG" || echo unknown)"
echo "=== captured schedule: $DISPATCH_COUNT dispatches -> $SCHEDULE_OUT ==="

# Done with ghOSt entirely from here -- the replay is a plain, unprivileged,
# single-threaded process. Let the agent teardown trap run early so it
# doesn't linger while we do the (unrelated) replay measurement.
ghost_teardown_agent
trap - EXIT

echo ""
echo "=== replaying the SAME schedule single-process, no ghOSt/threads/yields ==="
REPLAY_PERF_OUT="/tmp/perf_replay.txt"
# sudo (not for privilege the replay itself needs -- it's a plain
# unprivileged process) because kernel-mode counters (cycles:k) require it
# under this host's perf_event_paranoid=2, same as every other perf_*.sh
# script here.
REPLAY_ENV_ARGS=()
if [ -n "$QUIET_LOGGING" ]; then REPLAY_ENV_ARGS+=(QUIET_LOGGING="$QUIET_LOGGING"); fi
# cycles:u/instructions:u deliberately omitted: ScheduleReplay self-monitors
# those per-service via perf_event_open (see ../src/perf_counter.h) and
# prints them at the end of its own run -- more precisely scoped (per-call,
# excludes setup) than an external attach can be anyway, which can only give
# ONE combined total across both services here since it's a single thread.
# :k stays since self-monitoring is userspace-only (exclude_kernel=1).
sudo env "${REPLAY_ENV_ARGS[@]}" perf stat -e cycles:k,instructions:k,task-clock,context-switches \
  -o "$REPLAY_PERF_OUT" \
  -- "$REPLAY_BIN" "$SCHEDULE_OUT" "$UID_TRACE" "$MEDIA_TRACE"
echo "--- perf stats ($REPLAY_PERF_OUT), single-process replay ---"
cat "$REPLAY_PERF_OUT"
