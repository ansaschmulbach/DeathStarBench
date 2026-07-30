#ifndef SOCIAL_NETWORK_MICROSERVICES_TCPDUMP_FILE_SERVER_H_
#define SOCIAL_NETWORK_MICROSERVICES_TCPDUMP_FILE_SERVER_H_

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <cstddef>
#include <cstdlib>
#include <sched.h>
#include <string>
#include <nlohmann/json.hpp>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSSLSocket.h>
#include <thrift/transport/TSSLServerSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TFDTransport.h>
#include <thrift/transport/TTransportException.h>
#include "stream.h"

namespace social_network{
using json = nlohmann::json;
using apache::thrift::transport::TFDTransport;
using apache::thrift::TProcessor;
using apache::thrift::protocol::TTransport;
using apache::thrift::protocol::TProtocol;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TTransportException;

class TcpDumpFileServer {
public:
	TcpDumpFileServer(std::shared_ptr<TProcessor> processor, uint64_t port, std::string tcpflowReportFilename, std::shared_ptr<TTransport> transportOut, std::shared_ptr<TProtocol> protocolOut) : 
			processor(processor), 
			client(port, tcpflowReportFilename.c_str()), 
			transportOut(transportOut), 
			protocolOut(protocolOut)
			 	{ }

	void serve() {
		for (;;) {
				try {
					auto filename = client.streamData();
					if (filename == "") {
						LOG(error) << "ran out of files" << std::endl;
						break;
					}
					auto transportIn = openFileTransport(("traces/" + filename).c_str(), false);
					if (!transportIn) {
						LOG(error) << "could not open input trace file";
					}
  					std::shared_ptr<TProtocol> protocolIn(new TBinaryProtocol(transportIn));
					processor.get()->process(protocolIn, protocolOut, NULL);
					transportIn->close();
					static const bool skip_yield = std::getenv("GHOST_SKIP_YIELD") != nullptr;
					if (!skip_yield) sched_yield();
				} catch (TTransportException& ttx) {
					LOG(error) << "breaking: " << ttx.what();
					break;
				}
		}
	}
private:
		
		std::shared_ptr<TProcessor> processor;
		Client client;
		std::shared_ptr<TTransport> transportOut;
		std::shared_ptr<TProtocol> protocolOut;
};

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_TCPDUMP_FILE_SERVER_H_
