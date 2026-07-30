#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_INGRESS_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_INGRESS_H_

#include <chrono>
#include <random>

namespace social_network {

// Port of ghost-userspace's experiments/rocksdb/ingress.h Ingress class to
// plain C++14/std (no abseil, since this codebase doesn't otherwise depend
// on it). Same algorithm: a Poisson arrival process with lambda ==
// `throughput` (requests/sec), simulated via exponential inter-arrival
// sampling, polled (not blocking) via HasNewArrival().
class Ingress {
 public:
  explicit Ingress(double throughput) : throughput_(throughput), gen_(std::random_device{}()) {}

  void Start() { start_ = Clock::now(); }

  // Returns true if at least one synthetic request has "arrived" by now,
  // and advances the internal schedule to the next arrival. Mirrors the
  // original's polling contract exactly: call this repeatedly; each true
  // return corresponds to exactly one arrival.
  bool HasNewArrival() {
    if (Clock::now() >= start_) {
      start_ += NextDuration();
      return true;
    }
    return false;
  }

 private:
  using Clock = std::chrono::steady_clock;

  // Interarrival time for a Poisson process with lambda == throughput_
  // (requests/sec) is exponentially distributed. Convert to
  // requests/millisecond first, same as the original, to keep the
  // distribution's scale reasonable under double precision.
  Clock::duration NextDuration() {
    std::exponential_distribution<double> dist(throughput_ / 1000.0);
    double duration_msec = dist(gen_);
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double, std::milli>(duration_msec));
  }

  const double throughput_;
  Clock::time_point start_;
  std::mt19937_64 gen_;
};

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_INGRESS_H_
