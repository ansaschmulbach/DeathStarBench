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
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../utils_thrift.h"
#include "UniqueIdHandler.h"

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

  json config_json;
  if (load_config_file("config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  std::string netif = config_json["unique-id-service"]["netif"];

  std::string machine_id = GetMachineId(netif);
  if (machine_id == "") {
    exit(EXIT_FAILURE);
  }
  LOG(info) << "machine_id = " << machine_id;

  std::mutex thread_lock;

  const char *trace_file_env = std::getenv("TRACE_FILE");
  std::string trace_file = trace_file_env ? trace_file_env : "trace-unique-id-service";

	auto _transportIn = openFileTransport(trace_file.c_str(), false);
	if (!_transportIn) {
		LOG(error) << "could not open input trace file " << trace_file;
		exit(EXIT_FAILURE);
	}
  std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));

	auto _transportOut = openFileTransport("out-unique-id-service", true);
	if (!_transportOut) {
		LOG(error) << "could not open output trace file";
	}
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));

  TFileServer server(
      std::make_shared<UniqueIdServiceProcessor>(
          std::make_shared<UniqueIdHandler>(&thread_lock, machine_id)),
			_transportIn, _protocolIn, _transportOut, _protocolOut
		  );

  LOG(info) << "Starting the unique-id-service server ...";
  server.serve();
}
