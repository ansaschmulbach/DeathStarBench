#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
// #include <thrift/server/TSimpleServer.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../dump_file_server.h"
#include "../utils_thrift.h"
#include "MediaHandler.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TSimpleServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();
  // SetUpTracer("/data/sanchez/users/ansa/DSB/socialNetwork/config/jaeger-config.yml", "media-service");
  json config_json;
  if (load_config_file("/users/ansa/DeathStarBench/socialNetwork/config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  int port = config_json["media-service"]["port"];

  auto _transportOut = openFileTransport("out", true);
  if (!_transportOut) {
   LOG(error) << "could not open output trace file";
  }
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));
  
  auto _transportIn = openFileTransport(("tcpdump_out/media-service/" + std::to_string(port) + "-in").c_str(), false);
  if (!_transportIn) {
   LOG(error) << "could not open input trace file";
  }
  std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));

  FileServer server(
      std::make_shared<MediaServiceProcessor>(std::make_shared<MediaHandler>()),
	_transportIn,
	_protocolIn, 
	_transportOut,
	_protocolOut);

  LOG(info) << "Starting the media-service server...";
  server.serve();
}
