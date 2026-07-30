// Standalone verification tool: creates the shared-memory event log (see
// ../shm_log.h) that UniqueIdService/MediaService/CombinedService append to
// when SHM_LOG_NAME is set, watches it live, and once all expected events
// have arrived (or things go quiet for too long), prints a timeline sorted
// by wall time -- which core each request actually ran on and when, plus
// per-request durations. This process does nothing but observe: it never
// touches scheduling or request handling, so it can't perturb what it's
// measuring.
//
// For just the bare dispatch order (no cpu/duration analysis -- e.g. to
// feed ScheduleReplay), see ../ScheduleLogger, which shares this file's
// shm_log.h consumer helpers but prints much less.
//
// Usage:
//   Logger <shm_name> <capacity> <expected_events> [idle_timeout_sec=10] [timeline_out_file]
//
// Prints "[Logger] ready" (and flushes) as soon as the shm region exists, so
// a driving script can block on that line before launching the producer
// processes -- the region must exist before any producer's
// MaybeOpenShmLog() call, or that producer silently logs nothing.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

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
            "[idle_timeout_sec=10] [timeline_out_file]\n",
            argv[0]);
    return 1;
  }
  std::string shm_name = argv[1];
  uint32_t capacity = static_cast<uint32_t>(std::atoi(argv[2]));
  uint32_t expected_events = static_cast<uint32_t>(std::atoi(argv[3]));
  double idle_timeout_sec = argc > 4 ? std::atof(argv[4]) : 10.0;
  const char *timeline_out_path = argc > 5 ? argv[5] : nullptr;

  ShmLogRegion *region = CreateShmLog(shm_name.c_str(), capacity);
  if (!region) {
    perror("CreateShmLog");
    return 1;
  }

  printf("[Logger] ready: shm_name=%s capacity=%u expected_events=%u\n",
         shm_name.c_str(), capacity, expected_events);
  fflush(stdout);

  std::vector<ShmLogRecord> records =
      CollectShmLogEvents(region, expected_events, idle_timeout_sec);
  if (records.size() < expected_events) {
    fprintf(stderr,
            "[Logger] idle timeout with only %zu/%u events seen; printing what we have\n",
            records.size(), expected_events);
  }

  std::sort(records.begin(), records.end(),
            [](const ShmLogRecord &a, const ShmLogRecord &b) {
              return a.timestamp_ns < b.timestamp_ns;
            });

  std::ofstream out_file;
  if (timeline_out_path) out_file.open(timeline_out_path);

  // Only echo to stdout when there's no file -- at 100k+ requests, printing
  // every line floods the terminal for no reason once it's going to a file
  // anyway.
  bool echo_stdout = !out_file.is_open();
  auto emit = [&](const std::string &line) {
    if (echo_stdout) printf("%s\n", line.c_str());
    if (out_file.is_open()) out_file << line << "\n";
  };

  emit("=== timeline (" + std::to_string(records.size()) + " events) ===");
  emit("rel_time_us\tcpu\tpid\ttid\tlabel\tseq");

  uint64_t t0 = records.empty() ? 0 : records.front().timestamp_ns;
  int last_cpu = -1;
  char line[256];
  for (const auto &rec : records) {
    double rel_us = (rec.timestamp_ns - t0) / 1000.0;
    if (last_cpu != -1 && rec.cpu != last_cpu) {
      snprintf(line, sizeof(line), "  --- core switch: cpu %d -> %d ---", last_cpu, rec.cpu);
      emit(line);
    }
    snprintf(line, sizeof(line), "%10.1f\t%3d\t%6d\t%6d\t%-12s\t%u",
              rel_us, rec.cpu, rec.pid, rec.tid, rec.label, rec.seq);
    emit(line);
    last_cpu = rec.cpu;
  }

  // Pair up "<x>_start"/"<x>_end" records sharing the same (pid, seq) to
  // report per-request durations and highlight the gap between the two.
  // A given pid is always exactly one of {uid, media}, so (pid, seq) alone
  // is a unique key -- no need to also match on label. Building this map
  // first (O(n)) then doing O(1) lookups per start record is what keeps
  // this from being the O(n^2) nested-loop scan an earlier version had,
  // which was fine at ~100 requests but stalls out at 100k+ (400k+ records
  // -> tens of billions of comparisons).
  std::unordered_map<uint64_t, const ShmLogRecord *> end_by_key;
  end_by_key.reserve(records.size());
  for (const auto &rec : records) {
    std::string lbl(rec.label);
    if (lbl.size() > 4 && lbl.compare(lbl.size() - 4, 4, "_end") == 0) {
      end_by_key[(static_cast<uint64_t>(rec.pid) << 32) | rec.seq] = &rec;
    }
  }

  emit("");
  emit("=== per-request durations ===");
  emit("label\tseq\tpid\tstart_cpu\tend_cpu\tduration_us");
  for (const auto &start_rec : records) {
    std::string start_label(start_rec.label);
    auto suffix_pos = start_label.rfind("_start");
    if (suffix_pos == std::string::npos) continue;
    std::string base = start_label.substr(0, suffix_pos);

    auto it = end_by_key.find((static_cast<uint64_t>(start_rec.pid) << 32) | start_rec.seq);
    if (it == end_by_key.end()) continue;
    const ShmLogRecord &end_rec = *it->second;

    double dur_us = (end_rec.timestamp_ns - start_rec.timestamp_ns) / 1000.0;
    snprintf(line, sizeof(line), "%s\t%u\t%d\t%d\t%d\t%.1f",
              base.c_str(), start_rec.seq, start_rec.pid, start_rec.cpu,
              end_rec.cpu, dur_us);
    emit(line);
  }

  if (out_file.is_open()) out_file.close();
  DestroyShmLog(shm_name.c_str(), region, capacity);
  return 0;
}
