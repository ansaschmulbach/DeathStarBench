/*
 * 64-bit Unique Id Generator
 *
 * ------------------------------------------------------------------------
 * |0| 11 bit machine ID |      40-bit timestamp         | 12-bit counter |
 * ------------------------------------------------------------------------
 *
 * 11-bit machine Id code by hasing the MAC address
 * 40-bit UNIX timestamp in millisecond precision with custom epoch
 * 12 bit counter which increases monotonically on single process
 *
 */

#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../dump_file_server.h"
#include "../utils_thrift.h"
#include "UniqueIdHandler.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TSimpleServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();
  // SetUpTracer("/data/sanchez/users/ansa/DSB/socialNetwork/config/jaeger-config.yml", "unique-id-service");

  json config_json;
  if (load_config_file("/users/ansa/DeathStarBench/socialNetwork/config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  int port = config_json["unique-id-service"]["port"];
  std::string netif = config_json["unique-id-service"]["netif"];

  std::string machine_id = GetMachineId(netif);
  if (machine_id == "") {
    exit(EXIT_FAILURE);
  }
  LOG(info) << "machine_id = " << machine_id;

  auto _transportOut = openFileTransport("out", true);
  if (!_transportOut) {
   LOG(error) << "could not open output trace file";
  }
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));
  
  auto _transportIn = openFileTransport(("tcpdump_out/unique-id-service/" + std::to_string(port) + "-in").c_str(), false);
  if (!_transportIn) {
   LOG(error) << "could not open input trace file";
  }
  std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));

  std::mutex thread_lock;
  std::shared_ptr<TServerSocket> server_socket = get_server_socket(config_json, "localhost", port);
  FileServer server(
      std::make_shared<UniqueIdServiceProcessor>(
        std::make_shared<UniqueIdHandler>(&thread_lock, machine_id)),
	_transportIn,
	_protocolIn, 
	_transportOut,
	_protocolOut);

  LOG(info) << "Starting the unique-id-service server ...";
  server.serve();
}
