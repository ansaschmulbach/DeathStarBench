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
  init_logger();
  SetUpTracer("config/jaeger-config.yml", "media-service");
  json config_json;
  if (load_config_file("config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  int port = config_json["media-service"]["port"];

	auto _transportIn = openFileTransport("/social-network-microservices/socialnetwork-compose-post-service-1/media-servicetrace_out", false);
	if (!_transportIn) {
		LOG(error) << "could not open input trace file";
	}
  	std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));
	auto _transportOut = openFileTransport("out", true);
	if (!_transportOut) {
		LOG(error) << "could not open output trace file";
	}
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));
  TFileServer server(
      std::make_shared<MediaServiceProcessor>(std::make_shared<MediaHandler>()),
			_transportIn, _protocolIn, _transportOut, _protocolOut
			);

  LOG(info) << "Starting the media-service server...";
  server.serve();
}
