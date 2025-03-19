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
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../utils_thrift.h"
#include "UniqueIdHandler.h"

using apache::thrift::protocol::TJSONProtocolFactory;
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

  std::mutex thread_lock;
  std::shared_ptr<TServerSocket> server_socket = get_server_socket(config_json, "localhost", port);
  TSimpleServer server(
      std::make_shared<UniqueIdServiceProcessor>(
          std::make_shared<UniqueIdHandler>(&thread_lock, machine_id)),
      server_socket,
      std::make_shared<TFramedTransportFactory>(),
      std::make_shared<TJSONProtocolFactory>());

  LOG(info) << "Starting the unique-id-service server ...";
  server.serve();
}
