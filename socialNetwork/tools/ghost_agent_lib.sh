# Shared helpers for the perf_solo.sh / perf_cross_process.sh /
# perf_same_process.sh scripts: launching a ghOSt agent, waiting for it to
# come up, finding the enclave it created, and tearing it down cleanly.
# Not meant to be run directly -- source it.
#
# Requires GHOST_USERSPACE_DIR to point at a built ghost-userspace checkout
# (defaults to ../../../ghost-userspace, i.e. a sibling of this DeathStarBench
# checkout under $HOME). The ghOSt agent binaries need root (agent_bpf_init
# requires elevated capabilities), so every function here that touches
# /sys/fs/ghost or launches an agent uses sudo.

GHOST_USERSPACE_DIR="${GHOST_USERSPACE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../ghost-userspace" && pwd)}"

# Resolves the real PID of a `sudo <binary> &` invocation's direct child --
# the actual binary, not the sudo monitor. In this environment sudo doesn't
# exec-replace itself when backgrounded (it forks a monitor that supervises
# the real child instead), so `$!` right after `sudo foo &` is the monitor's
# PID, not foo's. That distinction matters both for signaling (see the NOTE
# in ghost_teardown_agent) and for `perf stat -p`, which needs the PID that's
# actually doing the work. Polls briefly since the child may not exist yet
# in the instant right after backgrounding.
sudo_child_pid() {
  local sudo_pid="$1"
  local child=""
  local waited=0
  until [ -n "$child" ]; do
    child="$(pgrep -P "$sudo_pid" 2>/dev/null | head -1)"
    [ -n "$child" ] && break
    sleep 0.01
    waited=$((waited + 1))
    if [ "$waited" -gt 500 ]; then  # 5s
      echo "[ghost_agent_lib] sudo_child_pid: timed out waiting for child of $sudo_pid" >&2
      return 1
    fi
  done
  echo "$child"
}

# Destroys any enclave currently sitting under /sys/fs/ghost with no agent
# attached (agent_online == 0). A dangling enclave from a previous crashed/
# interrupted run blocks a fresh agent from starting cleanly. Enclaves with
# a live agent are left alone.
ghost_cleanup_dangling_enclaves() {
  for enclave in /sys/fs/ghost/enclave_*; do
    [ -d "$enclave" ] || continue
    local online
    online="$(cat "$enclave/agent_online" 2>/dev/null || echo 1)"
    if [ "$online" = "0" ]; then
      echo "[ghost_agent_lib] destroying dangling enclave $enclave" >&2
      printf 'destroy' | sudo tee "$enclave/ctl" > /dev/null 2>&1 || true
    fi
  done
}

# Launches an agent binary (first arg) with the given flags (remaining args)
# in the background, waits for its "Initialization complete" message, and
# sets GHOST_AGENT_PID / GHOST_AGENT_LOG / GHOST_ENCLAVE_DIR for the caller.
#
# Usage: ghost_launch_agent <agent_binary> [agent_args...]
ghost_launch_agent() {
  local agent_bin="$1"
  shift

  ghost_cleanup_dangling_enclaves

  GHOST_AGENT_LOG="$(mktemp /tmp/ghost_agent_XXXXXX.log)"
  sudo "$agent_bin" "$@" > "$GHOST_AGENT_LOG" 2>&1 &
  GHOST_AGENT_PID=$!

  # Poll for the agent's startup banner rather than sleeping a fixed amount
  # -- the agent needs to init the BPF program and enclave, which can take
  # anywhere from tens to hundreds of ms.
  local waited=0
  until grep -q "Initialization complete" "$GHOST_AGENT_LOG" 2>/dev/null; do
    # NOTE: checked via /proc, not `kill -0` -- the agent runs as root (via
    # sudo) and `kill -0` from a non-root caller returns EPERM (nonzero) for
    # a live root-owned process just as it would for a dead one, which would
    # make this misreport a perfectly healthy agent as having exited.
    if [ ! -d "/proc/$GHOST_AGENT_PID" ]; then
      echo "[ghost_agent_lib] agent exited before completing init:" >&2
      cat "$GHOST_AGENT_LOG" >&2
      return 1
    fi
    sleep 0.05
    waited=$((waited + 1))
    if [ "$waited" -gt 200 ]; then  # 10s
      echo "[ghost_agent_lib] timed out waiting for agent init" >&2
      cat "$GHOST_AGENT_LOG" >&2
      return 1
    fi
  done

  # Find the (sole) enclave with a live agent attached. Enclave numbering
  # isn't reliable for before/after diffing -- a just-destroyed enclave's
  # slot can be reused immediately by the new agent under the SAME name, so
  # a plain directory-listing diff can see no change at all.
  GHOST_ENCLAVE_DIR=""
  for enclave in /sys/fs/ghost/enclave_*; do
    [ -d "$enclave" ] || continue
    if [ "$(cat "$enclave/agent_online" 2>/dev/null)" = "1" ]; then
      GHOST_ENCLAVE_DIR="$enclave"
    fi
  done
  if [ -z "$GHOST_ENCLAVE_DIR" ]; then
    echo "[ghost_agent_lib] could not find a live enclave after agent init" >&2
    return 1
  fi
  echo "[ghost_agent_lib] agent PID=$GHOST_AGENT_PID, enclave=$GHOST_ENCLAVE_DIR" >&2
}

# Sends SIGINT and waits for clean shutdown (the agent destroys its own
# enclave on the way out). Falls back to `pushtosched` to rescue any threads
# still stuck under SCHED_GHOST if the agent doesn't exit -- ghOSt tasks can
# be unresponsive to SIGKILL, which is why this exists as a distinct fallback
# rather than just `kill -9`.
ghost_teardown_agent() {
  [ -n "$GHOST_AGENT_PID" ] || return 0
  # NOTE: signal sudo's direct child (the actual agent binary), not the sudo
  # monitor PID itself. `sudo kill -INT <the sudo pid>` is unreliable when
  # this script is invoked as a nested bash child (e.g. run as a script file
  # rather than sourced/typed directly into a top-level shell) -- observed
  # in practice to leave the agent running indefinitely despite `sudo kill`
  # reporting success, seemingly because sudo's signal-forwarding-to-child
  # path depends on session/pty details that differ by nesting depth.
  # Signaling the real child directly sidesteps whatever that is.
  local real_pid
  real_pid="$(pgrep -P "$GHOST_AGENT_PID" 2>/dev/null | head -1)"
  sudo kill -INT "${real_pid:-$GHOST_AGENT_PID}" 2>/dev/null || true
  local waited=0
  # /proc existence, not `kill -0` -- see the note in ghost_launch_agent.
  while [ -d "/proc/$GHOST_AGENT_PID" ]; do
    sleep 0.1
    waited=$((waited + 1))
    if [ "$waited" -gt 150 ]; then  # 15s
      echo "[ghost_agent_lib] agent PID=$GHOST_AGENT_PID didn't exit; leaving it -- check for stuck ghOSt threads (util/pushtosched) and dangling enclaves manually" >&2
      break
    fi
  done
  ghost_cleanup_dangling_enclaves
}
