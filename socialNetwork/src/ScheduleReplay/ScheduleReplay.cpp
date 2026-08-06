// Single-process, single-thread replay of a captured dispatch schedule (see
// ../ScheduleLogger) that goes through the REAL Thrift transport/protocol/
// processor stack for both services -- the exact same processing path
// TFileServer::serve() (utils_thrift.h) uses: trace-file reads, Thrift
// framing/deserialization, TProcessor dispatch, handler execution, response
// serialization. Two independent processors (one per service, each reading
// its own trace file) sit in one plain for-loop; an if/else picks which one
// runs each iteration, driven by the captured schedule -- no threads, no
// ghOSt enrollment, no sched_yield, no futex, no scheduling machinery of any
// kind. This is the correct "floor" measurement: it isolates exactly the
// thing under test (scheduling/dispatch overhead) by removing ONLY that,
// rather than also stripping out the whole Thrift layer the way an earlier,
// cruder version of this tool did (calling UniqueIdHandler::ComposeUniqueId/
// MediaHandler::ComposeMedia directly with hand-reconstructed arguments,
// bypassing serialization entirely).
//
// The schedule file's seq numbers aren't needed here: each trace file's own
// transport tracks its own read position, so calling that service's
// processor->process() naturally consumes the next framed request from ITS
// trace file, in the exact order the trace was generated. The schedule only
// has to say whose turn it is.
//
// Usage: ScheduleReplay <schedule_file> <uid_trace_file> <media_trace_file>
// (use the SAME trace files the schedule was captured against, so request
// content and order match what the schedule expects)

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportException.h>

#include "../MediaService/MediaHandler.h"
#include "../UniqueIdService/UniqueIdHandler.h"
#include "../latency_stats.h"
#include "../logger.h"
#include "../perf_counter.h"
#include "../utils_thrift.h"

using namespace social_network;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TFramedTransport;
using apache::thrift::transport::TTransportException;

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <schedule_file> <uid_trace_file> <media_trace_file>\n", argv[0]);
    return 1;
  }

  // Respects QUIET_LOGGING same as the real services (see ../logger.h) --
  // without this, UniqueIdHandler::ComposeUniqueId's per-request LOG(debug)
  // falls back to boost::log's unfiltered default sink.
  init_logger();

  std::ifstream schedule_file(argv[1]);
  if (!schedule_file.is_open()) {
    fprintf(stderr, "could not open schedule file %s\n", argv[1]);
    return 1;
  }

  std::mutex uid_lock;
  auto uid_transport_in = openFileTransport(argv[2], false);
  if (!uid_transport_in) {
    fprintf(stderr, "could not open uid trace file %s\n", argv[2]);
    return 1;
  }
  std::shared_ptr<TProtocol> uid_protocol_in(new TBinaryProtocol(uid_transport_in));
  auto uid_mem_out = openMemoryTransport();
  std::shared_ptr<TTransport> uid_transport_out(new TFramedTransport(uid_mem_out));
  std::shared_ptr<TProtocol> uid_protocol_out(new TBinaryProtocol(uid_transport_out));
  UniqueIdServiceProcessor uid_processor(
      std::make_shared<UniqueIdHandler>(&uid_lock, "2d4"));

  auto media_transport_in = openFileTransport(argv[3], false);
  if (!media_transport_in) {
    fprintf(stderr, "could not open media trace file %s\n", argv[3]);
    return 1;
  }
  std::shared_ptr<TProtocol> media_protocol_in(new TBinaryProtocol(media_transport_in));
  auto media_mem_out = openMemoryTransport();
  std::shared_ptr<TTransport> media_transport_out(new TFramedTransport(media_mem_out));
  std::shared_ptr<TProtocol> media_protocol_out(new TBinaryProtocol(media_transport_out));
  MediaServiceProcessor media_processor(std::make_shared<MediaHandler>());

  // Per-request start->end timing, same "one clock read before, one after"
  // approach the real services' shm_log.h instrumentation uses, so the
  // measurement methodology is comparable, not just the code path. Kept in
  // memory (not shm) since this is single-process/single-threaded -- no
  // separate observer process is needed here. Throughput is computed from
  // each label's own first-start -> last-end span (matching Logger.cpp),
  // not the whole run's span, since uid and media don't each occupy the
  // full run the way a naive "total requests / total wall time" would imply.
  std::vector<double> uid_durations_ns, media_durations_ns;
  double uid_first_start_ns = 0, uid_last_end_ns = 0;
  double media_first_start_ns = 0, media_last_end_ns = 0;

  // Self-monitoring instructions:u/cycles:u counters (see ../perf_counter.h),
  // bracketed around each individual processor->process() call below. One
  // group object, shared by both services, since this is a single thread --
  // the per-call Read()-before/Read()-after delta is what separates uid's
  // cost from media's even though an EXTERNAL `perf stat -t <tid>` couldn't
  // (only one tid exists here). Deliberately per-call here, unlike
  // TFileServer::serve() (utils_thrift.h)'s whole-loop bracket: per-call is
  // the only way to get the uid/media split at all for a single alternating
  // thread, and unlike the real scheduled paths, there's no scheduler here
  // for the extra read() syscalls to perturb -- this is a synthetic replay
  // with no scheduling decisions in the loop to disturb, so the syscall cost
  // is a wash-through, not a confound. HardwareCounterGroup (rather than two
  // separate HardwareCounters) halves that syscall cost: one grouped read()
  // returns both instructions and cycles instead of two separate read()s.
  HardwareCounterGroup counters(PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_CPU_CYCLES);
  uint64_t uid_instr_u_total = 0, media_instr_u_total = 0;
  uint64_t uid_cycles_u_total = 0, media_cycles_u_total = 0;

  std::string label;
  uint32_t seq;  // unused (see header note) -- just consumed to advance past it
  uint32_t uid_count = 0, media_count = 0;
  bool uid_done = false, media_done = false;
  auto epoch = std::chrono::steady_clock::now();
  auto ns_since_epoch = [&](std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::nano>(t - epoch).count();
  };
  while (schedule_file >> label >> seq) {
    try {
      if (label == "uid" && !uid_done) {
        auto t0 = std::chrono::steady_clock::now();
        HardwareCounterGroup::Reading before = counters.Read();
        uid_processor.process(uid_protocol_in, uid_protocol_out, nullptr);
        HardwareCounterGroup::Reading after = counters.Read();
        uid_instr_u_total += after.leader_value - before.leader_value;
        uid_cycles_u_total += after.member_value - before.member_value;
        auto t1 = std::chrono::steady_clock::now();
        uid_durations_ns.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
        if (uid_count == 0) uid_first_start_ns = ns_since_epoch(t0);
        uid_last_end_ns = ns_since_epoch(t1);
        uid_count++;
      } else if (label == "media" && !media_done) {
        auto t0 = std::chrono::steady_clock::now();
        HardwareCounterGroup::Reading before = counters.Read();
        media_processor.process(media_protocol_in, media_protocol_out, nullptr);
        HardwareCounterGroup::Reading after = counters.Read();
        media_instr_u_total += after.leader_value - before.leader_value;
        media_cycles_u_total += after.member_value - before.member_value;
        auto t1 = std::chrono::steady_clock::now();
        media_durations_ns.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
        if (media_count == 0) media_first_start_ns = ns_since_epoch(t0);
        media_last_end_ns = ns_since_epoch(t1);
        media_count++;
      }
    } catch (TTransportException &ttx) {
      if (ttx.getType() == TTransportException::TTransportExceptionType::END_OF_FILE) {
        if (label == "uid") uid_done = true; else media_done = true;
      } else {
        fprintf(stderr, "breaking on %s: %s\n", label.c_str(), ttx.what());
        break;
      }
    }
  }

  fprintf(stderr, "replayed %u uid + %u media requests\n", uid_count, media_count);

  std::cerr << "=== instructions:u/cycles:u (processor->process() only, per service) ===\n"
            << "uid        n=" << uid_count
            << " instructions_u_total=" << uid_instr_u_total
            << " instructions_u_avg=" << (uid_count ? static_cast<double>(uid_instr_u_total) / uid_count : 0.0)
            << " cycles_u_total=" << uid_cycles_u_total
            << " cycles_u_avg=" << (uid_count ? static_cast<double>(uid_cycles_u_total) / uid_count : 0.0)
            << "\n"
            << "media      n=" << media_count
            << " instructions_u_total=" << media_instr_u_total
            << " instructions_u_avg=" << (media_count ? static_cast<double>(media_instr_u_total) / media_count : 0.0)
            << " cycles_u_total=" << media_cycles_u_total
            << " cycles_u_avg=" << (media_count ? static_cast<double>(media_cycles_u_total) / media_count : 0.0)
            << "\n";

  std::cerr << "=== latency (per request, processor->process() call only) ===\n";
  PrintLatencyStats(std::cerr, "uid",
                     ComputeLatencyStats(uid_durations_ns,
                                         (uid_last_end_ns - uid_first_start_ns) / 1e9));
  PrintLatencyStats(std::cerr, "media",
                     ComputeLatencyStats(media_durations_ns,
                                         (media_last_end_ns - media_first_start_ns) / 1e9));

  return 0;
}
