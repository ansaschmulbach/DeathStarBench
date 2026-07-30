#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../utils_thrift.h"
#include "MediaHandler.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  MaybeJoinGhostEnclave();
  MaybeAnnounceAndDelay("media");
  init_logger();

  const char *trace_file_env = std::getenv("TRACE_FILE");
  std::string trace_file = trace_file_env ? trace_file_env : "trace-media-service";

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
      _transportIn, _protocolIn, _transportOut, _protocolOut
      );

  LOG(info) << "Starting the media-service server...";
  server.serve();

  dumpMemoryTransportToFile(_memOut, "out-media-service");
}
