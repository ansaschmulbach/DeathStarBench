// A version of Logger (see ../Logger) that dumps nothing but the bare
// dispatch order -- no cpu/pid/tid/duration analysis, just "what ran when"
// as a plain "<uid|media> <seq>" line per completed request, one per line,
// in the order ghOSt actually dispatched them. That file is exactly the
// input ScheduleReplay needs to replay the same sequence of work in a
// single process with no scheduling machinery at all.
//
// A "_start" record only counts as a completed dispatch if there's a
// matching "_end" for the same (pid, seq, label) -- this filters out the
// one dangling "_start" logged when a service's request loop tries to read
// past the end of its trace file (TFileServer::serve() logs "_start"
// before calling process(), which is also where the end-of-trace exception
// gets thrown; there's no corresponding "_end" for that attempt since no
// request was actually processed). See utils_thrift.h.
//
// Usage:
//   ScheduleLogger <shm_name> <capacity> <expected_events> [idle_timeout_sec=10] [schedule_out_file]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_set>

#include "../shm_log.h"

using social_network::CollectShmLogEvents;
using social_network::CreateShmLog;
using social_network::DestroyShmLog;
using social_network::ShmLogRecord;
using social_network::ShmLogRegion;

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr,
            "usage: %s <shm_name> <capacity> <expected_events> "
            "[idle_timeout_sec=10] [schedule_out_file]\n",
            argv[0]);
    return 1;
  }
  std::string shm_name = argv[1];
  uint32_t capacity = static_cast<uint32_t>(std::atoi(argv[2]));
  uint32_t expected_events = static_cast<uint32_t>(std::atoi(argv[3]));
  double idle_timeout_sec = argc > 4 ? std::atof(argv[4]) : 10.0;
  const char *schedule_out_path = argc > 5 ? argv[5] : nullptr;

  ShmLogRegion *region = CreateShmLog(shm_name.c_str(), capacity);
  if (!region) {
    perror("CreateShmLog");
    return 1;
  }

  printf("[ScheduleLogger] ready: shm_name=%s capacity=%u expected_events=%u\n",
         shm_name.c_str(), capacity, expected_events);
  fflush(stdout);

  std::vector<ShmLogRecord> records =
      CollectShmLogEvents(region, expected_events, idle_timeout_sec);
  if (records.size() < expected_events) {
    fprintf(stderr,
            "[ScheduleLogger] idle timeout with only %zu/%u events seen; dumping what we have\n",
            records.size(), expected_events);
  }

  std::sort(records.begin(), records.end(),
            [](const ShmLogRecord &a, const ShmLogRecord &b) {
              return a.timestamp_ns < b.timestamp_ns;
            });

  std::ofstream out_file;
  if (schedule_out_path) out_file.open(schedule_out_path);

  // Only echo to stdout when there's no file -- at 100k+ requests, printing
  // every line floods the terminal for no reason once it's going to a file
  // anyway.
  bool echo_stdout = !out_file.is_open();
  auto emit = [&](const std::string &line) {
    if (echo_stdout) printf("%s\n", line.c_str());
    if (out_file.is_open()) out_file << line << "\n";
  };

  // A given pid is always exactly one of {uid, media}, so (pid, seq) alone
  // uniquely identifies a request -- no need to also key on label. Building
  // this set first turns the has-a-matching-end check into an O(1) lookup;
  // the naive nested-loop version (an O(n^2) linear rescan of `records` for
  // every start) is fine at the ~100-request scale this was first tested at
  // but pathological at 100k+ (400k+ records -> tens of billions of
  // comparisons, which is what stalled a 100k-request run out to a `timeout`
  // kill rather than finishing).
  std::unordered_set<uint64_t> ended;
  ended.reserve(records.size());
  for (const auto &rec : records) {
    std::string lbl(rec.label);
    if (lbl.size() > 4 && lbl.compare(lbl.size() - 4, 4, "_end") == 0) {
      ended.insert((static_cast<uint64_t>(rec.pid) << 32) | rec.seq);
    }
  }

  uint32_t dispatch_count = 0;
  for (const auto &start_rec : records) {
    std::string start_label(start_rec.label);
    auto suffix_pos = start_label.rfind("_start");
    if (suffix_pos == std::string::npos) continue;
    std::string base = start_label.substr(0, suffix_pos);

    uint64_t key = (static_cast<uint64_t>(start_rec.pid) << 32) | start_rec.seq;
    if (ended.find(key) == ended.end()) continue;  // dangling EOF-probe start

    emit(base + " " + std::to_string(start_rec.seq));
    dispatch_count++;
  }

  fprintf(stderr, "[ScheduleLogger] dumped %u completed dispatches\n", dispatch_count);

  if (out_file.is_open()) out_file.close();
  DestroyShmLog(shm_name.c_str(), region, capacity);
  return 0;
}
