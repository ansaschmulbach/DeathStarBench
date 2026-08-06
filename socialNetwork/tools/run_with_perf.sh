#!/bin/bash
# Runs UniqueIdService or MediaService under `perf stat -- <binary>`, letting
# perf launch and exec the binary itself rather than attaching to an
# already-running PID -- see the note below on why. This is the
# single-binary building block: point it at one service binary plus
# (optionally) an already-running ghOSt enclave's tasks file, and it reports
# that binary's own cycles/instructions, split into userspace (:u) and
# kernel (:k) portions to match the convention the rest of tools/ uses
# (perf_solo.sh, perf_cross_process.sh, perf_same_process.sh).
#
# Race-free by construction: `perf stat -- <command>` execs the binary
# itself, so counting starts before the binary's first instruction runs --
# there's no attach-after-launch race to lose events to. An earlier version
# of this script instead backgrounded the binary, grabbed its PID via $!,
# and attached `perf stat -p <pid>` after the fact; that raced the workload
# (a small trace can finish in well under a second) and could attach to a
# PID that had already exited by the time perf got there ("Problems finding
# threads of monitor"), silently producing no stats at all. See
# perf_solo.sh, which uses this same exec-wrap approach.
#
# Also runs the binary itself under sudo (not just perf): GHOST_ENCLAVE_TASKS
# points at a root-owned /sys/fs/ghost/.../tasks pseudo-file, and
# MaybeJoinGhostEnclave() (src/utils.h) silently no-ops on an EACCES open --
# so without sudo, setting GHOST_ENCLAVE_TASKS looked like it was enrolling
# the task into ghOSt but actually wasn't, and stats would silently be for
# the normal CFS scheduler instead.
#
# Env vars (all optional, same ones the binaries themselves read):
#   GHOST_ENCLAVE_TASKS  path to a ghOSt enclave's tasks file -- if set, the
#                        binary self-enrolls into it at startup (see
#                        MaybeJoinGhostEnclave() in src/utils.h). Leave
#                        unset on a non-ghOSt kernel, or to measure under the
#                        normal scheduler; it's a no-op there. This script
#                        does NOT launch an agent itself -- start one first
#                        (e.g. `source ghost_agent_lib.sh; ghost_launch_agent
#                        ...`) and pass its enclave's tasks file here.
#   GHOST_SKIP_YIELD     if set, skips the per-request sched_yield() call
#                        (see TFileServer::serve() in src/utils_thrift.h).
#                        Compare with/without to measure its overhead.
#   TRACE_FILE           input trace file (see tools/gen_*_trace.cpp). If
#                        unset, or set to a file that no longer exists, one
#                        is generated on the fly (1000 requests) into /tmp --
#                        there's no checked-in default trace file anywhere
#                        in the tree anymore, which is what made this
#                        script's old hardcoded fallback path silently fail
#                        ("could not open input trace file
#                        trace-unique-id-service"), exiting before perf
#                        could even attach.
#   QUIET_LOGGING        see ../src/logger.h. Defaults to 1 (quiet) here,
#                        same default the other perf_*.sh scripts use.
#
# Usage: run_with_perf.sh <path/to/UniqueIdService-or-MediaService> [perf_output_file]
#
# Example -- the exact yield vs. no-yield A/B from this branch's commits:
#   ./tools/run_with_perf.sh ./build/src/UniqueIdService/UniqueIdService /tmp/yield.txt
#   GHOST_SKIP_YIELD=1 ./tools/run_with_perf.sh ./build/src/UniqueIdService/UniqueIdService /tmp/noyield.txt
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOCIALNET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BIN="$1"
PERF_OUT="${2:-perf_stat.txt}"
BIN_ABS="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
BASENAME="$(basename "$BIN_ABS")"
QUIET_LOGGING="${QUIET_LOGGING:-1}"

TRACE_FILE_ABS=""
if [ -n "${TRACE_FILE:-}" ] && [ -f "$TRACE_FILE" ]; then
  TRACE_FILE_ABS="$(cd "$(dirname "$TRACE_FILE")" && pwd)/$(basename "$TRACE_FILE")"
else
  case "$BASENAME" in
    UniqueIdService)
      GEN_SRC="gen_uniqueid_trace"
      GEN_DEPS=("$SOCIALNET_DIR/gen-cpp/UniqueIdService.cpp" \
                "$SOCIALNET_DIR/gen-cpp/ComposePostService.cpp" \
                "$SOCIALNET_DIR/gen-cpp/social_network_types.cpp")
      ;;
    MediaService)
      GEN_SRC="gen_media_trace"
      GEN_DEPS=("$SOCIALNET_DIR/gen-cpp/MediaService.cpp" \
                "$SOCIALNET_DIR/gen-cpp/social_network_types.cpp")
      ;;
    *)
      echo "no usable TRACE_FILE given and don't know how to generate one for" \
           "'$BASENAME' (expected UniqueIdService or MediaService) -- pass" \
           "TRACE_FILE=<path> explicitly" >&2
      exit 1
      ;;
  esac
  GEN_BIN="/tmp/${GEN_SRC}"
  if [ ! -x "$GEN_BIN" ]; then
    echo "=== building $GEN_SRC ==="
    g++ -std=c++14 -O2 -I "$SOCIALNET_DIR/gen-cpp" "$SOCIALNET_DIR/tools/${GEN_SRC}.cpp" \
      "${GEN_DEPS[@]}" -lthrift -lpthread -o "$GEN_BIN"
  fi
  TRACE_FILE_ABS="$(mktemp "/tmp/trace_${BASENAME}_XXXXXX")"
  echo "=== generating 1000-request trace for $BASENAME -> $TRACE_FILE_ABS ==="
  "$GEN_BIN" "$TRACE_FILE_ABS" 1000 > /dev/null
fi

ENV_ARGS=(TRACE_FILE="$TRACE_FILE_ABS")
if [ -n "${GHOST_ENCLAVE_TASKS:-}" ]; then ENV_ARGS+=(GHOST_ENCLAVE_TASKS="$GHOST_ENCLAVE_TASKS"); fi
if [ -n "${GHOST_SKIP_YIELD:-}" ]; then ENV_ARGS+=(GHOST_SKIP_YIELD="$GHOST_SKIP_YIELD"); fi
if [ -n "$QUIET_LOGGING" ]; then ENV_ARGS+=(QUIET_LOGGING="$QUIET_LOGGING"); fi

echo "=== running $BASENAME under perf (trace=$TRACE_FILE_ABS) ==="
cd "$(dirname "$BIN_ABS")"
# cycles:u/instructions:u deliberately omitted: the binary self-monitors
# those via perf_event_open (see ../src/perf_counter.h) and prints them at
# the end of its own run -- more precisely scoped than an external attach,
# and asking perf for the same hardware events on top of that self-
# monitoring oversubscribes the CPU's limited physical PMU counters. :k
# stays since self-monitoring is userspace-only (exclude_kernel=1).
sudo env "${ENV_ARGS[@]}" perf stat \
  -e cycles:k,instructions:k,task-clock,context-switches \
  -o "$PERF_OUT" \
  -- "./$BASENAME"

echo "--- perf stats ($PERF_OUT) ---"
cat "$PERF_OUT"
