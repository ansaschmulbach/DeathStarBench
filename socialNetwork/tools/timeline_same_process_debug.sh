#!/bin/bash
# Same-process (CombinedService) scenario, instrumented end-to-end: the
# ghost-userspace agent runs with GHOST_TIMELINE_DEBUG=1 (see
# schedulers/ab_alternator/ab_scheduler.{h,cc} on the ab-alternator-
# affinity-fix branch of https://github.com/ansaschmulbach/ghost-userspace),
# recording a nanosecond-precision internal timeline of every dispatch
# decision (avail_done/dequeue/txn_open/commit/task_on_cpu, plus every
# TASK_YIELD/TASK_BLOCKED/... message the agent's poll loop sees, with how
# many empty channel checks preceded it), while CombinedService's own
# shm_log.h timeline (see timeline_cross_process.sh for the same mechanism)
# independently timestamps each request's start/end on the SAME clock
# (CLOCK_MONOTONIC -- ghost-userspace's MonotonicNow() and shm_log.h's
# clock_gettime(CLOCK_MONOTONIC) calls are directly comparable, confirmed by
# a same-process cross-check that agreed to within 67ns).
#
# Together these let analyze_yield_timeline.py reconstruct an exact,
# real (not synthetic) sequence of events from one thread's sched_yield()
# call through the other thread's sched_yield() return -- see that script's
# own header for what it does with the two output files this produces.
#
# Requires ghost-userspace's ab_alternator_agent built from the
# ab-alternator-affinity-fix branch (or later) -- NOT plain
# ab_alternator_scheduler HEAD, which never checks a task's cpu affinity
# before dispatching or migrating onto it (see that branch's commit message
# for the full investigation). Without that fix, workers intermittently land
# on the agent's own cpu and the whole measurement is unreliable.
#
# Escape valve: the agent's own PickNextGlobalCPU() (see the same commit
# message) periodically relocates its "global cpu" role in reaction to
# ordinary (non-ghost) system load wanting the agent's cpu -- and, since it
# never checks affinity either, will preempt whatever worker happens to be
# on its migration target. Giving it a THIRD enclave cpu that no worker's
# affinity ever includes (this script auto-picks the --ghost_cpus agent
# cpu's own hyperthread sibling, since PickNextGlobalCPU() checks siblings
# first before falling through to any other candidate) means it always has
# somewhere to go that has nothing to preempt. Falls back to a bare 2-cpu
# enclave (agent + worker only) with a warning if the agent cpu has no
# sibling (e.g. SMT disabled) -- expect occasional TASK_PREEMPT-driven
# outliers in that case; analyze_yield_timeline.py's robust pairing handles
# them, just with a smaller clean fraction.
#
# Usage:
#   ./tools/timeline_same_process_debug.sh [num_requests] [agent_log_out] [shm_timeline_out]
#
# Then:
#   python3 ./tools/analyze_yield_timeline.py <agent_log_out> <shm_timeline_out>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/ghost_agent_lib.sh"

SOCIALNET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMBINED_BIN="$SOCIALNET_DIR/build/src/CombinedService/CombinedService"
LOGGER_BIN="$SOCIALNET_DIR/build/src/Logger/Logger"
GEN_UID_SRC="$SOCIALNET_DIR/tools/gen_uniqueid_trace.cpp"
GEN_MEDIA_SRC="$SOCIALNET_DIR/tools/gen_media_trace.cpp"

NUM_REQUESTS="${1:-100000}"
AGENT_LOG_OUT="${2:-/tmp/yield_timeline_agent.log}"
SHM_TIMELINE_OUT="${3:-/tmp/yield_timeline_shm.txt}"
AGENT_CPU="${GHOST_AGENT_CPU:-1}"
WORKER_CPU="${GHOST_WORKER_CPU:-2}"
STARTUP_DELAY_MS="${STARTUP_DELAY_MS:-800}"

for bin in "$COMBINED_BIN" "$LOGGER_BIN"; do
  if [ ! -x "$bin" ]; then
    echo "missing $bin -- build it first: (cd build && make CombinedService Logger)" >&2
    exit 1
  fi
done

# Escape-valve cpu: the agent cpu's own hyperthread sibling, if it has one.
ESCAPE_CPU="${GHOST_ESCAPE_CPU:-}"
if [ -z "$ESCAPE_CPU" ]; then
  siblings=($(expand_cpu_list "$(cat "/sys/devices/system/cpu/cpu$AGENT_CPU/topology/thread_siblings_list" 2>/dev/null)"))
  for s in "${siblings[@]}"; do
    [ "$s" != "$AGENT_CPU" ] && { ESCAPE_CPU="$s"; break; }
  done
fi
if [ -n "$ESCAPE_CPU" ]; then
  GHOST_CPUS="$AGENT_CPU,$WORKER_CPU,$ESCAPE_CPU"
  echo "=== escape valve: cpu$ESCAPE_CPU (sibling of agent cpu$AGENT_CPU) ==="
else
  GHOST_CPUS="$AGENT_CPU,$WORKER_CPU"
  echo "=== WARNING: no hyperthread sibling found for cpu$AGENT_CPU -- running without an escape valve." >&2
  echo "    Expect occasional TASK_PREEMPT-driven outliers; analyze_yield_timeline.py's robust" >&2
  echo "    pairing handles them, just with a smaller clean fraction. ===" >&2
fi

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

ghost_cleanup_dangling_enclaves
# ghost_pin_cpus is also called automatically by ghost_launch_agent (it
# parses --ghost_cpus out of the args below), but GHOST_TIMELINE_DEBUG needs
# to be set as an env var on the agent process itself, which
# ghost_launch_agent's plain `sudo "$agent_bin" "$@"` doesn't support -- so
# this launches the agent directly instead of via ghost_launch_agent, and
# duplicates just the pieces of its logic this needs (init-banner wait,
# live-enclave discovery, EXIT-trap teardown).
ghost_pin_cpus "$GHOST_CPUS"
# Both stdout (the "Initialization complete" banner and friends) and stderr
# (everything GHOST_TIMELINE_DEBUG prints) go into the same AGENT_LOG_OUT --
# matches ghost_agent_lib.sh's own convention elsewhere, and
# analyze_yield_timeline.py only pattern-matches specific line prefixes, so
# the banner lines mixed in are harmless noise to it.
sudo env GHOST_TIMELINE_DEBUG=1 "$GHOST_USERSPACE_DIR/bazel-bin/ab_alternator_agent" \
  --ghost_cpus="$GHOST_CPUS" --globalcpu="$AGENT_CPU" > "$AGENT_LOG_OUT" 2>&1 &
GHOST_AGENT_PID=$!
waited=0
until grep -q "Initialization complete" "$AGENT_LOG_OUT" 2>/dev/null; do
  if [ ! -d "/proc/$GHOST_AGENT_PID" ]; then
    echo "agent exited before completing init:" >&2
    cat "$AGENT_LOG_OUT" >&2
    ghost_unpin_cpus
    exit 1
  fi
  sleep 0.05
  waited=$((waited + 1))
  if [ "$waited" -gt 200 ]; then
    echo "timed out waiting for agent init" >&2
    cat "$AGENT_LOG_OUT" >&2
    ghost_unpin_cpus
    exit 1
  fi
done
waited=0
GHOST_ENCLAVE_DIR=""
while [ -z "$GHOST_ENCLAVE_DIR" ]; do
  if [ ! -d "/proc/$GHOST_AGENT_PID" ]; then
    echo "agent exited before an enclave came online:" >&2
    cat "$AGENT_LOG_OUT" >&2
    ghost_unpin_cpus
    exit 1
  fi
  for enclave in /sys/fs/ghost/enclave_*; do
    [ -d "$enclave" ] || continue
    [ "$(cat "$enclave/agent_online" 2>/dev/null)" = "1" ] && GHOST_ENCLAVE_DIR="$enclave"
  done
  sleep 0.05
  waited=$((waited + 1))
  if [ "$waited" -gt 400 ]; then
    echo "timed out waiting for a live enclave" >&2
    ghost_unpin_cpus
    exit 1
  fi
done
trap ghost_teardown_agent EXIT
echo "=== agent pid=$GHOST_AGENT_PID enclave=$GHOST_ENCLAVE_DIR (GHOST_TIMELINE_DEBUG=1, --ghost_cpus=$GHOST_CPUS) ==="

TASKS_FILE="$GHOST_ENCLAVE_DIR/tasks"
EVENTS_PER_REQUEST=2  # start+end
EXPECTED_EVENTS=$((NUM_REQUESTS * EVENTS_PER_REQUEST * 2))  # x2 services
SHM_CAPACITY=$((EXPECTED_EVENTS + 100))
SHM_NAME="/yield_timeline_$$"
LOGGER_LOG="$(mktemp /tmp/logger_XXXXXX.log)"

LOGGER_CPU="$(pick_isolated_cpu "$GHOST_CPUS")"
echo "=== launching Logger (shm=$SHM_NAME, expecting $EXPECTED_EVENTS events, pinned to cpu $LOGGER_CPU) ==="
taskset -c "$LOGGER_CPU" "$LOGGER_BIN" "$SHM_NAME" "$SHM_CAPACITY" "$EXPECTED_EVENTS" 20 "$SHM_TIMELINE_OUT" > "$LOGGER_LOG" 2>&1 &
LOGGER_PID=$!
waited=0
until grep -q "\[Logger\] ready" "$LOGGER_LOG" 2>/dev/null; do
  if [ ! -d "/proc/$LOGGER_PID" ]; then echo "Logger exited before becoming ready:" >&2; cat "$LOGGER_LOG" >&2; exit 1; fi
  sleep 0.02
  waited=$((waited + 1))
  if [ "$waited" -gt 500 ]; then echo "timed out waiting for Logger" >&2; cat "$LOGGER_LOG" >&2; exit 1; fi
done

COMBINED_LOG="$(mktemp /tmp/combined_XXXXXX.log)"
ENV_ARGS=(GHOST_ENCLAVE_TASKS="$TASKS_FILE" TRACE_FILE_UID="$UID_TRACE" TRACE_FILE_MEDIA="$MEDIA_TRACE"
          SHM_LOG_NAME="$SHM_NAME" STARTUP_DELAY_MS="$STARTUP_DELAY_MS" QUIET_LOGGING=1)

echo "=== launching CombinedService (STARTUP_DELAY_MS=$STARTUP_DELAY_MS) ==="
( cd "$(dirname "$COMBINED_BIN")" && sudo env "${ENV_ARGS[@]}" "./$(basename "$COMBINED_BIN")" > "$COMBINED_LOG" 2>&1 ) &
COMBINED_SHELL_PID=$!

UID_TID=""; MEDIA_TID=""
waited=0
while [ -z "$UID_TID" ] || [ -z "$MEDIA_TID" ]; do
  UID_TID="$(grep -oP '(?<=\[startup_delay\] uid tid=)\d+' "$COMBINED_LOG" 2>/dev/null || true)"
  MEDIA_TID="$(grep -oP '(?<=\[startup_delay\] media tid=)\d+' "$COMBINED_LOG" 2>/dev/null || true)"
  sleep 0.05
  waited=$((waited + 1))
  if [ "$waited" -gt 400 ]; then echo "timed out waiting for both thread TIDs" >&2; cat "$COMBINED_LOG" >&2; exit 1; fi
done
echo "uid tid=$UID_TID media tid=$MEDIA_TID"

# Restrict each worker to WORKER_CPU specifically -- excludes both the agent
# cpu AND the escape valve, so PickNextGlobalCPU() migrating onto the escape
# valve never has a worker there to preempt. See ghost_worker_cpus()'s own
# comment (tools/ghost_agent_lib.sh) for why this is a hard guarantee only
# because of the ab-alternator-affinity-fix branch's Dequeue()/
# GlobalSchedule() changes -- stock ab_alternator ignores this entirely.
sudo taskset -pc "$WORKER_CPU" "$UID_TID" > /dev/null 2>&1
sudo taskset -pc "$WORKER_CPU" "$MEDIA_TID" > /dev/null 2>&1

wait "$COMBINED_SHELL_PID"
echo "CombinedService exited"
wait "$LOGGER_PID"
echo "Logger done"

# ghost_teardown_agent (EXIT trap) sends SIGINT, which triggers
# ~FifoScheduler()'s DumpTimelineRecords() -- the buffered [timeline]/
# [timeline-msg]/[timeline-bench] lines only get written to AGENT_LOG_OUT at
# that point, not during the run. Let the trap run before reporting done.
echo ""
echo "=== outputs ==="
echo "AGENT_LOG_OUT=$AGENT_LOG_OUT"
echo "SHM_TIMELINE_OUT=$SHM_TIMELINE_OUT"
echo ""
echo "Next: python3 $SCRIPT_DIR/analyze_yield_timeline.py $AGENT_LOG_OUT $SHM_TIMELINE_OUT"
