#ifndef SOCIAL_NETWORK_MICROSERVICES_PERF_COUNTER_H
#define SOCIAL_NETWORK_MICROSERVICES_PERF_COUNTER_H

// Self-monitoring hardware counters via perf_event_open(2) -- instructions
// and cycles, both userspace-only (exclude_kernel/exclude_hv, matching this
// codebase's ":u" perf stat convention elsewhere).
//
// Every other perf measurement in this repo is external: `perf stat -p/-t`
// or `perf stat -- <command>`, attached from outside the process. External
// attach has two problems for isolating "just the worker loop's own work":
// (1) it can only bracket a whole process/thread lifetime, not an arbitrary
// code region inside it, so it necessarily also counts setup (opening trace
// files, constructing Thrift processors, and -- for a `-- <command>` wrap
// specifically -- dynamic linking/relocation before main() even runs); (2)
// it's one counter per OS thread, so it can't separate two different
// services' costs when both are served by the SAME thread (as in
// ScheduleReplay's single-threaded uid/media alternation).
//
// This sidesteps both: open one counter for the calling thread, then bracket
// any code region with Read()-before/Read()-after and take the delta. The
// delta only reflects work done *during that bracket*, so calling it around
// the whole request-processing loop -- not around setup -- excludes setup
// and any other process/thread's work, while still costing only two read()
// syscalls total (one per bracket edge) rather than two per request. Two
// per request was tried first and rejected: on a workload this
// syscall-cheap to begin with, injecting a syscall around every single
// request measurably adds to what's being measured (and, worse, perturbs
// the very cache/scheduling behavior a scheduling-overhead comparison cares
// about) -- see the whole-loop bracket in utils_thrift.h's
// TFileServer::serve(). ScheduleReplay still brackets per-call, since that's
// what makes its uid-vs-media split possible in the first place (one
// thread, two services, no external `perf -t` can split that) -- it isn't
// measuring anything a real scheduler could be perturbed by, so the syscall
// cost there is a wash-through, not a confound.
//
// Read() takes one syscall regardless of how many HardwareCounter instances
// are read -- they're independent perf_event fds, not a grouped read. When
// two counters are always read together (e.g. instructions+cycles
// bracketed around the same region), HardwareCounterGroup below opens them
// as one perf_event GROUP so a single read() returns both, halving syscall
// count for that case -- see its class comment.
//
// Uses PERF_FORMAT_TOTAL_TIME_ENABLED/RUNNING and scales accordingly. Not
// optional: with two counters per thread now (instructions + cycles), on
// top of whatever hardware events an external `perf stat -t`/`-p` wrapper
// has simultaneously attached, it's easy to oversubscribe the CPU's limited
// physical PMU counters. The kernel's answer to oversubscription is time-
// multiplexing (round-robining events onto the hardware), not failure -- a
// plain unscaled read() silently returns however much got counted while
// resident on real hardware, which can land far below the true value or, if
// an event never won a slot during a short bracket, exactly 0 -- observed
// in practice on this host: two threads (uid, media) each self-monitoring
// instructions+cycles, measured by an external perf attach ALSO reading
// cycles:u/cycles:k/instructions:u/instructions:k for both threads, and one
// thread's self-monitored cycles total came back as a bare 0 without this
// scaling.



#include <cstdint>
#include <cstring>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace social_network {

class HardwareCounter {
 public:
  // `hw_config` is one of the PERF_COUNT_HW_* constants, e.g.
  // PERF_COUNT_HW_INSTRUCTIONS or PERF_COUNT_HW_CPU_CYCLES.
  explicit HardwareCounter(uint32_t hw_config) {
    struct perf_event_attr attr;
    std::memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(attr);
    attr.config = hw_config;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.disabled = 0;  // start counting immediately; only Read() deltas matter.
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    // pid=0 (calling thread/self), cpu=-1 (follow the thread to whatever CPU
    // it runs on) -- the standard self-monitoring incantation. No group
    // leader (group_fd=-1), no flags.
    fd_ = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
  }

  ~HardwareCounter() {
    if (fd_ >= 0) close(fd_);
  }

  HardwareCounter(const HardwareCounter &) = delete;
  HardwareCounter &operator=(const HardwareCounter &) = delete;

  bool valid() const { return fd_ >= 0; }

  // Cumulative count since the counter was opened, scaled to correct for
  // PMU multiplexing (see the class comment). Callers take the difference
  // of two reads to get a delta for whatever ran in between. Returns 0 (a
  // safe no-op delta) if the counter failed to open, e.g. under a
  // perf_event_paranoid setting that blocks it -- see the note in
  // perf_solo.sh about this host's paranoid=2 -- or if it never once got
  // scheduled onto real hardware (time_running == 0), in which case there's
  // no measured count to scale from and 0 is the honest answer.
  uint64_t Read() const {
    if (fd_ < 0) return 0;
    struct {
      uint64_t value;
      uint64_t time_enabled;
      uint64_t time_running;
    } sample;
    if (read(fd_, &sample, sizeof(sample)) != static_cast<ssize_t>(sizeof(sample))) {
      return 0;
    }
    if (sample.time_running == 0) return 0;
    if (sample.time_running >= sample.time_enabled) return sample.value;
    return static_cast<uint64_t>(static_cast<double>(sample.value) *
                                  sample.time_enabled / sample.time_running);
  }

 private:
  int fd_ = -1;
};

// Two hardware counters (e.g. instructions + cycles) opened as one
// perf_event GROUP so a single read() syscall returns both values, instead
// of one read() per counter -- HardwareCounter's Read() is one syscall
// EACH, so bracketing two counters before/after a region costs 4 syscalls;
// this costs 2. Same pid=0/cpu=-1 self-monitoring scoping, same
// TOTAL_TIME_ENABLED/RUNNING multiplexing-correction rationale as
// HardwareCounter -- see its class comment.
//
// Does NOT help split two different logical streams sharing one thread
// (e.g. ScheduleReplay's uid/media alternation): perf_event counters are
// scoped to a task, not a code region, so there's no flag that makes a
// counter count only "while running code path A" -- both streams still
// land in the one accumulated value regardless of how many fds are open.
// This only cuts the per-request syscall count for whichever single stream
// each bracket already measures (still Read()-before/Read()-after per
// service, just one syscall instead of two per edge).
class HardwareCounterGroup {
 public:
  struct Reading {
    uint64_t leader_value;
    uint64_t member_value;
  };

  // `leader_hw_config`/`member_hw_config` are PERF_COUNT_HW_* constants,
  // e.g. (PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_CPU_CYCLES).
  HardwareCounterGroup(uint32_t leader_hw_config, uint32_t member_hw_config) {
    struct perf_event_attr leader_attr;
    std::memset(&leader_attr, 0, sizeof(leader_attr));
    leader_attr.type = PERF_TYPE_HARDWARE;
    leader_attr.size = sizeof(leader_attr);
    leader_attr.config = leader_hw_config;
    leader_attr.exclude_kernel = 1;
    leader_attr.exclude_hv = 1;
    leader_attr.disabled = 0;
    leader_attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                               PERF_FORMAT_TOTAL_TIME_RUNNING |
                               PERF_FORMAT_GROUP;
    // group_fd=-1: this fd becomes the group leader.
    leader_fd_ = static_cast<int>(
        syscall(SYS_perf_event_open, &leader_attr, 0, -1, -1, 0));

    struct perf_event_attr member_attr;
    std::memset(&member_attr, 0, sizeof(member_attr));
    member_attr.type = PERF_TYPE_HARDWARE;
    member_attr.size = sizeof(member_attr);
    member_attr.config = member_hw_config;
    member_attr.exclude_kernel = 1;
    member_attr.exclude_hv = 1;
    member_attr.disabled = 0;
    member_attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                               PERF_FORMAT_TOTAL_TIME_RUNNING |
                               PERF_FORMAT_GROUP;
    // group_fd=leader_fd_: joins the leader's group. Only the leader fd is
    // ever read() -- reading a member fd directly would report just its
    // own value, not the group.
    member_fd_ = leader_fd_ >= 0
                     ? static_cast<int>(syscall(SYS_perf_event_open,
                                                 &member_attr, 0, -1,
                                                 leader_fd_, 0))
                     : -1;
  }

  ~HardwareCounterGroup() {
    if (member_fd_ >= 0) close(member_fd_);
    if (leader_fd_ >= 0) close(leader_fd_);
  }

  HardwareCounterGroup(const HardwareCounterGroup &) = delete;
  HardwareCounterGroup &operator=(const HardwareCounterGroup &) = delete;

  bool valid() const { return leader_fd_ >= 0 && member_fd_ >= 0; }

  // One read() syscall for both values -- see HardwareCounter::Read() for
  // the multiplexing-scaling rationale (identical here, applied to both
  // values using the group's single shared time_enabled/time_running,
  // since grouped events are always scheduled onto the PMU together).
  Reading Read() const {
    if (!valid()) return {0, 0};
    struct {
      uint64_t nr;
      uint64_t time_enabled;
      uint64_t time_running;
      uint64_t values[2];  // [0]=leader, [1]=member -- creation order.
    } sample;
    if (read(leader_fd_, &sample, sizeof(sample)) !=
        static_cast<ssize_t>(sizeof(sample))) {
      return {0, 0};
    }
    if (sample.nr != 2 || sample.time_running == 0) return {0, 0};
    double scale = sample.time_running >= sample.time_enabled
                       ? 1.0
                       : static_cast<double>(sample.time_enabled) /
                             sample.time_running;
    return {static_cast<uint64_t>(sample.values[0] * scale),
            static_cast<uint64_t>(sample.values[1] * scale)};
  }

 private:
  int leader_fd_ = -1;
  int member_fd_ = -1;
};

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_PERF_COUNTER_H
