#!/bin/bash
# Measures UniqueIdService and MediaService running as two SEPARATE OS
# processes, both enrolled into the same ghOSt enclave and scheduled by the
# ab_alternator agent (schedulers/ab_alternator in ghost-userspace), which
# strictly alternates dispatch between exactly two tasks on one worker CPU.
# This is the "cross-process alternating" scenario: every hand-off between
# the two request types pays a full address-space switch (CR3 reload / TLB
# domain switch) on top of ghOSt's own dispatch overhead, since the two
# tasks belong to different processes (different mm).
#
# Unlike perf_solo.sh, this can't use `perf stat -- <command>`: that only
# wraps a single command, and here there are two independent processes to
# sum counts across. Instead this leans on the same STARTUP_DELAY_MS hook
# CombinedService uses (see MaybeAnnounceAndDelay() in ../utils.h, wired up
# in UniqueIdService.cpp/MediaService.cpp's main()): each process joins the
# ghost enclave, announces its PID, and sleeps before doing any real work,
# giving a reliable window to attach `perf stat -p pid1,pid2` (which perf
# aggregates into one combined report) before either one can make progress.
#
# An earlier version tried to synchronize by SIGSTOPping both processes
# immediately after launch and resuming them once perf was attached. That
# was abandoned after it produced a stuck run: it's not safe to assume
# SIGSTOP/SIGCONT behave normally on a task that may already be under
# SCHED_GHOST by the time the signal lands -- ghOSt tasks are already known
# (from this repo's own history) to sometimes go unresponsive to ordinary
# signals. The STARTUP_DELAY_MS approach never touches a ghost-scheduled
# task with a job-control signal, so it doesn't have that failure mode.
#
# --ghost_cpus is deliberately restricted to exactly ONE worker CPU (plus
# the agent CPU) via GHOST_CPUS=1-2 below. With 2+ worker CPUs, ghOSt's
# GlobalSchedule() can dispatch both tasks to different CPUs in the same
# round and they'd run in true parallel instead of alternating on a shared
# core -- defeating the point of this measurement.
#
# Reports a per-service breakdown (perf's --per-thread against the two
# PIDs), not just a combined total -- UniqueIdService and MediaService do
# different amounts of work per request, so summing them hides that.
#
# QUIET_LOGGING=1 is set on both services by default (see ../src/logger.h):
# per-request LOG(debug) calls are already filtered out by init_logger()'s
# default (>= info), but this also silences the one-time startup LOG(info)
# lines, for a completely clean logging-free run. Unset QUIET_LOGGING (env)
# to get logs back.
#
# Usage:
#   ./tools/perf_cross_process.sh [uid_trace] [media_trace] [perf_output_file] [startup_delay_ms]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/ghost_agent_lib.sh"

SOCIALNET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
UID_BIN="$SOCIALNET_DIR/build/src/UniqueIdService/UniqueIdService"
MEDIA_BIN="$SOCIALNET_DIR/build/src/MediaService/MediaService"

UID_TRACE="${1:-$SOCIALNET_DIR/../../social-network-microservices/trace-unique-id-service}"
MEDIA_TRACE="${2:-$SOCIALNET_DIR/../../social-network-microservices/trace-media-service}"
PERF_OUT="${3:-perf_cross_process.txt}"
STARTUP_DELAY_MS="${4:-6000}"
GHOST_CPUS="${GHOST_CPUS:-1-2}"  # agent CPU + exactly one worker CPU -- see header note
QUIET_LOGGING="${QUIET_LOGGING:-1}"

echo "=== launching ab_alternator_agent (--ghost_cpus=$GHOST_CPUS) ==="
ghost_launch_agent "$GHOST_USERSPACE_DIR/bazel-bin/ab_alternator_agent" --ghost_cpus="$GHOST_CPUS"
trap ghost_teardown_agent EXIT

TASKS_FILE="$GHOST_ENCLAVE_DIR/tasks"
UID_LOG="$(mktemp /tmp/uid_XXXXXX.log)"
MEDIA_LOG="$(mktemp /tmp/media_XXXXXX.log)"

ENV_ARGS=(GHOST_ENCLAVE_TASKS="$TASKS_FILE" STARTUP_DELAY_MS="$STARTUP_DELAY_MS")
if [ -n "$QUIET_LOGGING" ]; then ENV_ARGS+=(QUIET_LOGGING="$QUIET_LOGGING"); fi

echo "=== launching UniqueIdService and MediaService (STARTUP_DELAY_MS=$STARTUP_DELAY_MS, QUIET_LOGGING=$QUIET_LOGGING), both enrolled in $TASKS_FILE ==="
sudo env "${ENV_ARGS[@]}" TRACE_FILE="$UID_TRACE" \
  "$UID_BIN" > "$UID_LOG" 2>&1 &
UID_SUDO_PID=$!
sudo env "${ENV_ARGS[@]}" TRACE_FILE="$MEDIA_TRACE" \
  "$MEDIA_BIN" > "$MEDIA_LOG" 2>&1 &
MEDIA_SUDO_PID=$!

# Poll both logs for their announced PID. Each process prints exactly once,
# right after joining the enclave and before its startup sleep.
UID_PID=""
MEDIA_PID=""
waited=0
while [ -z "$UID_PID" ] || [ -z "$MEDIA_PID" ]; do
  UID_PID="$(grep -oP '(?<=\[startup_delay\] uid tid=)\d+' "$UID_LOG" 2>/dev/null || true)"
  MEDIA_PID="$(grep -oP '(?<=\[startup_delay\] media tid=)\d+' "$MEDIA_LOG" 2>/dev/null || true)"
  sleep 0.05
  waited=$((waited + 1))
  if [ "$waited" -gt 200 ]; then  # 10s
    echo "timed out waiting for both PIDs to be announced" >&2
    cat "$UID_LOG" "$MEDIA_LOG" >&2
    exit 1
  fi
done
echo "UniqueIdService PID=$UID_PID, MediaService PID=$MEDIA_PID"

sudo perf stat -p "$UID_PID,$MEDIA_PID" --per-thread \
  -e cycles:u,cycles:k,instructions:u,instructions:k,task-clock,context-switches \
  -o "$PERF_OUT" &
PERF_SUDO_PID=$!

# Give perf a moment to actually attach before the announced processes'
# sleep elapses and real work starts.
sleep 0.3

wait "$UID_SUDO_PID"; UID_EXIT=$?
wait "$MEDIA_SUDO_PID"; MEDIA_EXIT=$?
echo "UniqueIdService exited $UID_EXIT, MediaService exited $MEDIA_EXIT"

# perf usually notices both tracked PIDs exit and stops on its own; this is
# a backstop. Signal perf's real child directly, not the sudo monitor PID --
# see the NOTE in ghost_teardown_agent for why that distinction matters.
PERF_PID="$(sudo_child_pid "$PERF_SUDO_PID" 2>/dev/null || true)"
sudo kill -INT "${PERF_PID:-$PERF_SUDO_PID}" 2>/dev/null || true
wait "$PERF_SUDO_PID" 2>/dev/null || true

echo "--- perf stats ($PERF_OUT), per-process ---"
cat "$PERF_OUT"
echo "--- UniqueIdService log ($UID_LOG) ---"; cat "$UID_LOG"
echo "--- MediaService log ($MEDIA_LOG) ---"; cat "$MEDIA_LOG"
