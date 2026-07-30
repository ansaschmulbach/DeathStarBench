// Same-process counterpart to running UniqueIdService and MediaService as
// two separate OS processes. Both request types are served here by two
// threads of ONE process, each self-enrolled into the ghost enclave as its
// own task (MaybeJoinGhostEnclave() enrolls whichever thread calls it, not
// the process as a whole). This isolates the effect of the address-space
// switch: cross-process alternation pays a CR3 reload/TLB-domain switch on
// every hand-off between A and B; same-process (same mm) alternation does
// not, since both tasks share the same page tables.

#include <signal.h>
#include <sys/syscall.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#include "../utils.h"
#include "../utils_thrift.h"
#include "../MediaService/MediaHandler.h"
#include "../UniqueIdService/UniqueIdHandler.h"

using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TFramedTransport;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

// MaybeAnnounceAndDelay() (used below, right after each thread's
// MaybeJoinGhostEnclave()) is shared with UniqueIdService/MediaService --
// see its definition in ../utils.h.

void RunUniqueIdThread() {
  MaybeJoinGhostEnclave();
  MaybeAnnounceAndDelay("uid");

  std::string machine_id = "2d4";
  std::mutex thread_lock;

  const char *trace_file_env = std::getenv("TRACE_FILE_UID");
  std::string trace_file =
      trace_file_env ? trace_file_env : "trace-unique-id-service";

  auto _transportIn = openFileTransport(trace_file.c_str(), false);
  if (!_transportIn) {
    LOG(error) << "could not open input trace file " << trace_file;
    exit(EXIT_FAILURE);
  }
  std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));

  auto _memOut = openMemoryTransport();
  std::shared_ptr<TTransport> _transportOut(new TFramedTransport(_memOut));
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));

  TFileServer server(
      std::make_shared<UniqueIdServiceProcessor>(
          std::make_shared<UniqueIdHandler>(&thread_lock, machine_id)),
      _transportIn, _protocolIn, _transportOut, _protocolOut);

  LOG(info) << "Starting the unique-id-service thread ...";
  server.serve();

  dumpMemoryTransportToFile(_memOut, "out-unique-id-service-combined");
}

void RunMediaThread() {
  MaybeJoinGhostEnclave();
  MaybeAnnounceAndDelay("media");

  const char *trace_file_env = std::getenv("TRACE_FILE_MEDIA");
  std::string trace_file =
      trace_file_env ? trace_file_env : "trace-media-service";

  auto _transportIn = openFileTransport(trace_file.c_str(), false);
  if (!_transportIn) {
    LOG(error) << "could not open input trace file " << trace_file;
    exit(EXIT_FAILURE);
  }
  std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));

  auto _memOut = openMemoryTransport();
  std::shared_ptr<TTransport> _transportOut(new TFramedTransport(_memOut));
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));

  TFileServer server(
      std::make_shared<MediaServiceProcessor>(std::make_shared<MediaHandler>()),
      _transportIn, _protocolIn, _transportOut, _protocolOut);

  LOG(info) << "Starting the media-service thread ...";
  server.serve();

  dumpMemoryTransportToFile(_memOut, "out-media-service-combined");
}

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();

  std::thread uid_thread(RunUniqueIdThread);
  std::thread media_thread(RunMediaThread);

  uid_thread.join();
  media_thread.join();
}
