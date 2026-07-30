#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../utils_thrift.h"
#include "../tcpflow_file_server.h"
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
  init_logger();
  SetUpTracer("config/jaeger-config.yml", "media-service");
  json config_json;
  if (load_config_file("config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  int port = config_json["media-service"]["port"];

  auto _transportOut = openFileTransport("out-media-service", true);
  if (!_transportOut) {
    LOG(error) << "could not open output trace file";
  }
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));
  std::string reportFilename = "/social-network-microservices/report-media.xml";

  TcpDumpFileServer server(
      std::make_shared<MediaServiceProcessor>(std::make_shared<MediaHandler>()),
      port, reportFilename, _transportOut, _protocolOut
      );

  LOG(info) << "Starting the media-service server...";
  server.serve();
}
