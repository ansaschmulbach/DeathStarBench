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

# 2 events (start+end) per request per service.
EXPECTED_EVENTS=$((NUM_REQUESTS * 2 * 2))
SHM_NAME="/timeline_$$"
LOGGER_LOG="$(mktemp /tmp/logger_XXXXXX.log)"

echo "=== launching Logger (shm=$SHM_NAME, expecting $EXPECTED_EVENTS events) ==="
"$LOGGER_BIN" "$SHM_NAME" 10000 "$EXPECTED_EVENTS" 15 "$TIMELINE_OUT" > "$LOGGER_LOG" 2>&1 &
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

echo "=== launching UniqueIdService and MediaService, both enrolled in $TASKS_FILE ==="
sudo env GHOST_ENCLAVE_TASKS="$TASKS_FILE" TRACE_FILE="$UID_TRACE" SHM_LOG_NAME="$SHM_NAME" \
  "$UID_BIN" > "$UID_LOG" 2>&1 &
UID_SUDO_PID=$!
sudo env GHOST_ENCLAVE_TASKS="$TASKS_FILE" TRACE_FILE="$MEDIA_TRACE" SHM_LOG_NAME="$SHM_NAME" \
  "$MEDIA_BIN" > "$MEDIA_LOG" 2>&1 &
MEDIA_SUDO_PID=$!

wait "$UID_SUDO_PID"; UID_EXIT=$?
wait "$MEDIA_SUDO_PID"; MEDIA_EXIT=$?
echo "UniqueIdService exited $UID_EXIT, MediaService exited $MEDIA_EXIT"

wait "$LOGGER_SUDO_PID"
echo "=== Logger output ==="
cat "$LOGGER_LOG"
echo ""
echo "(timeline also written to $TIMELINE_OUT)"
