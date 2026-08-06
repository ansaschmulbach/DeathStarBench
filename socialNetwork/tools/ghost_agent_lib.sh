# Shared helpers for the perf_solo.sh / perf_cross_process.sh /
# perf_same_process.sh scripts: launching a ghOSt agent, waiting for it to
# come up, finding the enclave it created, and tearing it down cleanly. Also
# pins cpufreq/C-states on the agent's cpus for the duration of the run (see
# ghost_pin_cpus) so run-to-run DVFS/idle-state variance doesn't get mixed
# into whatever's being measured. Not meant to be run directly -- source it.
#
# Requires GHOST_USERSPACE_DIR to point at a built ghost-userspace checkout
# (defaults to ../../../ghost-userspace, i.e. a sibling of this DeathStarBench
# checkout under $HOME). The ghOSt agent binaries need root (agent_bpf_init
# requires elevated capabilities), so every function here that touches
# /sys/fs/ghost, cpufreq/cpuidle sysfs, or launches an agent uses sudo.

GHOST_USERSPACE_DIR="${GHOST_USERSPACE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../ghost-userspace" && pwd)}"

# Expands a Linux CPU-list spec ("1-2", "0,3-5", the contents of
# /sys/.../cpu/online or a thread_siblings_list file) into a space-separated
# list of individual CPU numbers.
expand_cpu_list() {
  local spec="$1"
  local part lo hi i
  local result=()
  IFS=',' read -ra parts <<< "$spec"
  for part in "${parts[@]}"; do
    if [[ "$part" == *-* ]]; then
      lo="${part%-*}"; hi="${part#*-}"
      for ((i = lo; i <= hi; i++)); do result+=("$i"); done
    elif [ -n "$part" ]; then
      result+=("$part")
    fi
  done
  echo "${result[@]}"
}

# Returns the "worker" CPUs from a --ghost_cpus spec: every CPU in the spec
# EXCEPT the first (lowest-numbered) one, comma-joined for `taskset -c`.
# Matches the ghost-userspace agents' own default --globalcpu (the front of
# --ghost_cpus, unless overridden) -- i.e. this is "every CPU the agent
# might actually dispatch a task onto" MINUS the one it's supposed to
# reserve for itself.
#
# A taskset restriction to this function's output is now a hard guarantee
# that a worker never lands on the agent's own CPU -- but only because
# ghost-userspace's schedulers/ab_alternator/ab_scheduler.cc has been
# patched to make it one. The stock version trusted the userspace agent's
# dispatch decisions completely and never checked them against a task's
# cpumask at all (GlobalSchedule()/Available()/Dequeue() never referenced
# affinity), so a `taskset -pc` restriction was silently ignored -- confirmed
# empirically: workers were observed running on the agent's own CPU roughly
# as often as their intended one. The fix (ported from
# schedulers/fifo_affinity/, a second, pre-existing but unused scheduler in
# this same ghost-userspace checkout that already solved this) adds
# SchedGetAffinity()-backed affinity tracking (TaskNew()/
# TaskAffinityChanged()) and makes Dequeue()/GlobalSchedule() only assign a
# task to a CPU its own affinity mask actually permits, while preserving
# ab_alternator's strict A/B turn-taking policy. `exclude_spec` (optional,
# same expand_cpu_list()-style format) is no longer needed for correctness
# but is kept as a general-purpose way to drop additional CPUs from the
# result if some other reason to do so ever comes up.
ghost_worker_cpus() {
  local ghost_cpus_spec="$1"
  local exclude_spec="${2:-}"
  local cpus=($(expand_cpu_list "$ghost_cpus_spec"))
  local exclude=($(expand_cpu_list "$exclude_spec"))
  local result=() c e skip
  for c in "${cpus[@]:1}"; do
    skip=0
    for e in "${exclude[@]}"; do
      [ "$c" = "$e" ] && { skip=1; break; }
    done
    [ "$skip" = 0 ] && result+=("$c")
  done
  local IFS=','
  echo "${result[*]}"
}

# Picks a CPU for an out-of-band observer process (Logger/ScheduleLogger) to
# run on, given the CPU spec (e.g. "1-2") a ghOSt agent was launched with.
# Avoids every CPU the agent/workload are using AND their hyperthread twins
# -- an observer pinned to the HT sibling of a measured CPU still shares that
# core's L1/L2 cache and execution units with it, so it would perturb the
# very thing it's supposed to be watching from outside, just less obviously
# than sharing the CPU outright would. Returns the lowest-numbered online
# CPU not in that avoid-set.
pick_isolated_cpu() {
  local ghost_cpus_spec="$1"
  local c s
  local used=($(expand_cpu_list "$ghost_cpus_spec"))
  local avoid=()
  for c in "${used[@]}"; do
    avoid+=("$c")
    for s in $(expand_cpu_list "$(cat "/sys/devices/system/cpu/cpu$c/topology/thread_siblings_list" 2>/dev/null)"); do
      avoid+=("$s")
    done
  done
  local online=($(expand_cpu_list "$(cat /sys/devices/system/cpu/online 2>/dev/null)"))
  for c in "${online[@]}"; do
    local skip=0
    for a in "${avoid[@]}"; do
      [ "$c" = "$a" ] && { skip=1; break; }
    done
    [ "$skip" = 0 ] && { echo "$c"; return 0; }
  done
  echo "[ghost_agent_lib] pick_isolated_cpu: no free CPU avoiding {${avoid[*]}} (spec=$ghost_cpus_spec)" >&2
  return 1
}

# Pins cpufreq to max (performance governor + min=max) and disables every
# real cpuidle sleep state (leaving only POLL, 0us latency) on every cpu in
# `cpu_list_spec` (an expand_cpu_list()-style spec). Saves prior state into
# the _GHOST_SAVED_* arrays so ghost_unpin_cpus can put it back.
#
# Exists because this repo's own microbenchmarks measure single-digit-
# microsecond scheduling handoffs (see the ghOSt yield-latency investigation
# in tools/perf_same_process.sh et al). Two things undermine that at this
# resolution, independent of anything ghOSt does:
#   - C-state wake latency: this host's C6 alone has a ~133us exit latency
#     (see /sys/devices/system/cpu/cpu*/cpuidle/state*/latency) -- over 6x
#     the ~20us handoff being measured. If the CPU is idle long enough to
#     drop into it between bursts, the NEXT burst's first few requests eat a
#     wake-up cost that has nothing to do with ghOSt's dispatch overhead.
#   - cpufreq's `ondemand` governor sits at its idle floor (1.2GHz on this
#     host, vs. a 2.4-3.2GHz base/turbo range) for a workload this bursty --
#     confirmed empirically: a busy-loop pinned to a worker cpu measured
#     0.499 "CPUs utilized" (perf stat) under ondemand at idle frequency,
#     vs. 1.000 once pinned. The exact same cycle count then corresponds to
#     a run-to-run-varying number of nanoseconds, which is exactly the
#     ambiguity that came up trying to convert a cycle-based kernel-function
#     profile into a nanosecond figure.
# Requires sudo (writes to root-owned sysfs files).
declare -A _GHOST_SAVED_GOVERNOR
declare -A _GHOST_SAVED_MIN_FREQ
declare -A _GHOST_SAVED_MAX_FREQ
declare -A _GHOST_SAVED_CSTATE_DISABLE  # cpu -> "idx:val idx:val ..."
_GHOST_PINNED_CPUS=""

ghost_pin_cpus() {
  local cpu_list_spec="$1"
  local cpus=($(expand_cpu_list "$cpu_list_spec"))
  local cpu
  for cpu in "${cpus[@]}"; do
    local freq_dir="/sys/devices/system/cpu/cpu$cpu/cpufreq"
    local idle_dir="/sys/devices/system/cpu/cpu$cpu/cpuidle"
    if [ -d "$freq_dir" ]; then
      _GHOST_SAVED_GOVERNOR[$cpu]="$(cat "$freq_dir/scaling_governor" 2>/dev/null)"
      _GHOST_SAVED_MIN_FREQ[$cpu]="$(cat "$freq_dir/scaling_min_freq" 2>/dev/null)"
      _GHOST_SAVED_MAX_FREQ[$cpu]="$(cat "$freq_dir/scaling_max_freq" 2>/dev/null)"
      local max_freq
      max_freq="$(cat "$freq_dir/cpuinfo_max_freq" 2>/dev/null)"
      if [ -n "$max_freq" ]; then
        echo performance | sudo tee "$freq_dir/scaling_governor" > /dev/null 2>&1
        echo "$max_freq" | sudo tee "$freq_dir/scaling_max_freq" > /dev/null 2>&1
        echo "$max_freq" | sudo tee "$freq_dir/scaling_min_freq" > /dev/null 2>&1
      fi
    fi
    if [ -d "$idle_dir" ]; then
      local flags="" state_dir idx name cur
      for state_dir in "$idle_dir"/state*; do
        [ -d "$state_dir" ] || continue
        idx="${state_dir##*state}"
        name="$(cat "$state_dir/name" 2>/dev/null)"
        cur="$(cat "$state_dir/disable" 2>/dev/null)"
        flags="$flags $idx:$cur"
        # POLL (state0, 0us latency) isn't a real sleep state -- leave it
        # alone. Disable every actual sleep state (C1/C1E/C3/C6/...) so the
        # idle loop spins instead of paying a wake-up latency mid-handoff.
        if [ "$name" != "POLL" ]; then
          echo 1 | sudo tee "$state_dir/disable" > /dev/null 2>&1
        fi
      done
      _GHOST_SAVED_CSTATE_DISABLE[$cpu]="$flags"
    fi
  done
  _GHOST_PINNED_CPUS="${cpus[*]}"
  echo "[ghost_agent_lib] pinned cpus {${cpus[*]}} to performance governor @ max freq, disabled C-states beyond POLL" >&2
}

# Restores whatever ghost_pin_cpus last saved. Safe to call even if
# ghost_pin_cpus was never called (no-op).
ghost_unpin_cpus() {
  [ -n "$_GHOST_PINNED_CPUS" ] || return 0
  local cpu
  for cpu in $_GHOST_PINNED_CPUS; do
    local freq_dir="/sys/devices/system/cpu/cpu$cpu/cpufreq"
    local idle_dir="/sys/devices/system/cpu/cpu$cpu/cpuidle"
    if [ -d "$freq_dir" ] && [ -n "${_GHOST_SAVED_GOVERNOR[$cpu]:-}" ]; then
      # max before min: writing min > current max is rejected by some
      # drivers mid-transition.
      echo "${_GHOST_SAVED_MAX_FREQ[$cpu]}" | sudo tee "$freq_dir/scaling_max_freq" > /dev/null 2>&1
      echo "${_GHOST_SAVED_MIN_FREQ[$cpu]}" | sudo tee "$freq_dir/scaling_min_freq" > /dev/null 2>&1
      echo "${_GHOST_SAVED_GOVERNOR[$cpu]}" | sudo tee "$freq_dir/scaling_governor" > /dev/null 2>&1
    fi
    if [ -d "$idle_dir" ] && [ -n "${_GHOST_SAVED_CSTATE_DISABLE[$cpu]:-}" ]; then
      local pair idx val
      for pair in ${_GHOST_SAVED_CSTATE_DISABLE[$cpu]}; do
        idx="${pair%%:*}"; val="${pair#*:}"
        echo "$val" | sudo tee "$idle_dir/state$idx/disable" > /dev/null 2>&1
      done
    fi
  done
  echo "[ghost_agent_lib] restored cpufreq governor/C-states on cpus {$_GHOST_PINNED_CPUS}" >&2
  _GHOST_PINNED_CPUS=""
}

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

  # Pin cpufreq/C-states on whatever cpus the agent is about to own -- see
  # ghost_pin_cpus's comment for why. Every call site here passes
  # --ghost_cpus=<spec>, so pull it back out of "$@" rather than adding a
  # separate parameter every caller would need to thread through.
  local arg ghost_cpus_spec=""
  for arg in "$@"; do
    case "$arg" in
      --ghost_cpus=*) ghost_cpus_spec="${arg#--ghost_cpus=}" ;;
    esac
  done
  if [ -n "$ghost_cpus_spec" ]; then
    ghost_pin_cpus "$ghost_cpus_spec"
  fi

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
      ghost_unpin_cpus
      return 1
    fi
    sleep 0.05
    waited=$((waited + 1))
    if [ "$waited" -gt 200 ]; then  # 10s
      echo "[ghost_agent_lib] timed out waiting for agent init" >&2
      cat "$GHOST_AGENT_LOG" >&2
      ghost_unpin_cpus
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
    ghost_unpin_cpus
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
  ghost_unpin_cpus
}
