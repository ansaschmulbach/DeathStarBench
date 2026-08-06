#ifndef SOCIAL_NETWORK_MICROSERVICES_LATENCY_STATS_H
#define SOCIAL_NETWORK_MICROSERVICES_LATENCY_STATS_H

// Latency percentile computation, ported from ghost-userspace's
// experiments/rocksdb/latency.h/.cc (min/50/99/99.5/99.9/max + throughput,
// via percentile-index lookup into a sorted duration vector). Ported rather
// than linked directly: that's a Bazel target in a different repo with a
// different build system (this is a plain CMake build with no path to
// depend on ghost-userspace's targets), and it's built around that
// experiment's 5-stage RocksDB request pipeline (ingress queue -> handle ->
// worker queue -> worker handle, via named absl::Time fields on a Request
// struct) -- overkill for what we have here, which is just one stage
// (request start -> end) per request. The percentile math itself (sort,
// then index by fraction*size, with the same "size-1 when even" adjustment
// so the index lands on a real sample) is copied as-is from that source.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

namespace social_network {

struct LatencyStats {
  size_t total = 0;
  double throughput_per_sec = 0;
  double min_us = 0, p50_us = 0, p99_us = 0, p995_us = 0, p999_us = 0, max_us = 0;
};

// durations_ns: one entry per request, in nanoseconds. Need not be sorted --
// this sorts its own copy. runtime_sec is the wall-clock span the requests
// were generated over, used only for the throughput figure.
inline LatencyStats ComputeLatencyStats(std::vector<double> durations_ns,
                                         double runtime_sec) {
  LatencyStats stats;
  if (durations_ns.empty()) return stats;
  std::sort(durations_ns.begin(), durations_ns.end());

  stats.total = durations_ns.size();
  stats.throughput_per_sec = runtime_sec > 0 ? stats.total / runtime_sec : 0;

  // Same indexing ghost-userspace's latency.cc uses: when the count is even,
  // index against (count - 1) so fraction*size lands on an existing sample
  // instead of needing interpolation.
  size_t size = stats.total % 2 == 0 ? stats.total - 1 : stats.total;
  auto at_us = [&](double frac) {
    return durations_ns[static_cast<size_t>(size * frac)] / 1000.0;
  };

  stats.min_us = durations_ns.front() / 1000.0;
  stats.p50_us = at_us(0.5);
  stats.p99_us = at_us(0.99);
  stats.p995_us = at_us(0.995);
  stats.p999_us = at_us(0.999);
  stats.max_us = durations_ns.back() / 1000.0;
  return stats;
}

inline void PrintLatencyStats(std::ostream &os, const std::string &label,
                               const LatencyStats &s) {
  if (s.total == 0) {
    os << label << ": no requests\n";
    return;
  }
  os << std::left << std::setw(10) << label << " "
     << "n=" << std::setw(9) << s.total
     << "throughput=" << std::setw(11) << static_cast<uint64_t>(s.throughput_per_sec)
     << " req/s   "
     << std::fixed << std::setprecision(1)
     << "min=" << std::setw(9) << s.min_us
     << "p50=" << std::setw(9) << s.p50_us
     << "p99=" << std::setw(9) << s.p99_us
     << "p99.5=" << std::setw(9) << s.p995_us
     << "p99.9=" << std::setw(9) << s.p999_us
     << "max=" << std::setw(9) << s.max_us
     << "(us)\n";
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_LATENCY_STATS_H
