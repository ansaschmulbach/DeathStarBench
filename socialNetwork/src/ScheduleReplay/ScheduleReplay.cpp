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

#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportException.h>

#include "../MediaService/MediaHandler.h"
#include "../UniqueIdService/UniqueIdHandler.h"
#include "../logger.h"
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

  std::string label;
  uint32_t seq;  // unused (see header note) -- just consumed to advance past it
  uint32_t uid_count = 0, media_count = 0;
  bool uid_done = false, media_done = false;
  while (schedule_file >> label >> seq) {
    try {
      if (label == "uid" && !uid_done) {
        uid_processor.process(uid_protocol_in, uid_protocol_out, nullptr);
        uid_count++;
      } else if (label == "media" && !media_done) {
        media_processor.process(media_protocol_in, media_protocol_out, nullptr);
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
  return 0;
}
