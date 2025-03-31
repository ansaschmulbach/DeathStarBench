#ifndef SOCIAL_NETWORK_MICROSERVICES_TCPDUMP_FILE_SERVER_H_
#define SOCIAL_NETWORK_MICROSERVICES_TCPDUMP_FILE_SERVER_H_

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <cstddef>
#include <string>
#include <nlohmann/json.hpp>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSSLSocket.h>
#include <thrift/transport/TSSLServerSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TFDTransport.h>
#include <thrift/transport/TTransportException.h>
#include <thrift/server/TServer.h>
#include <thrift/transport/TServerTransport.h>
#include <thrift/transport/TTransport.h>

#include <thread>
#include <chrono>

#include "logger.h"
#include "utils_thrift.h"

namespace social_network{
using json = nlohmann::json;
using apache::thrift::transport::TFDTransport;
using apache::thrift::TProcessor;
using apache::thrift::protocol::TTransport;
using apache::thrift::protocol::TProtocol;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TTransportException;
using apache::thrift::transport::TTransportFactory;
using apache::thrift::protocol::TProtocolFactory;

class FileServer {
public:
	FileServer(std::shared_ptr<TProcessor> processor, std::shared_ptr<TTransport> transportIn, std::shared_ptr<TProtocol> protocolIn, std::shared_ptr<TTransport> transportOut, std::shared_ptr<TProtocol> protocolOut) : 
			processor(processor), 
			transportIn(transportIn), 
			protocolIn(protocolIn),
			transportOut(transportOut), 
			protocolOut(protocolOut)
			 	{ }

	void serve() {

		for (;;) {
				try {

					processor.get()->process(protocolIn, protocolOut, NULL);
					std::this_thread::sleep_for(std::chrono::seconds(10));
					// LOG(info) << "success!" << std::endl;
				} catch (TTransportException& ttx) {
					LOG(error) << "breaking: " << ttx.what();
					break;
				}
		}
	}
private:
		
	std::string filename;
	std::shared_ptr<TProcessor> processor;
	std::shared_ptr<TTransport> transportIn;
	std::shared_ptr<TProtocol> protocolIn;
	std::shared_ptr<TTransport> transportOut;
	std::shared_ptr<TProtocol> protocolOut;
};

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_TCPDUMP_FILE_SERVER_H_
