#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_

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

namespace social_network{
using json = nlohmann::json;
using apache::thrift::transport::TServerSocket;
using apache::thrift::transport::TSSLServerSocket;
using apache::thrift::transport::TSSLSocketFactory;
using apache::thrift::transport::TFramedTransport;
using apache::thrift::transport::TFDTransport;
using apache::thrift::TProcessor;
using apache::thrift::protocol::TTransport;
using apache::thrift::protocol::TProtocol;
using apache::thrift::transport::TTransportException;

std::shared_ptr<TServerSocket> get_server_socket(const json &config_json, const std::string &address, int port) {
  bool ssl_enabled = config_json["ssl"]["enabled"];
  if (ssl_enabled) {
    std::string cert_path = config_json["ssl"]["serverCertPath"];
    std::string key_path = config_json["ssl"]["serverKeyPath"];
    std::string ca_path = config_json["ssl"]["caPath"];
    std::string ciphers = config_json["ssl"]["ciphers"];

    std::shared_ptr<TSSLSocketFactory> ssl_socket_factory;
    ssl_socket_factory = std::make_shared<TSSLSocketFactory>();
    ssl_socket_factory->loadCertificate(cert_path.c_str());
    ssl_socket_factory->loadPrivateKey(key_path.c_str());
    ssl_socket_factory->ciphers(ciphers);
    // if (config_json["ssl"]["verifyClient"]) {
    //   ssl_socket_factory->loadTrustedCertificates(ca_path.c_str());
    //   ssl_socket_factory->authenticate(true);
    // }
    return std::make_shared<TSSLServerSocket>(address, port, ssl_socket_factory);
  }
  return std::make_shared<TServerSocket>(address, port);
};

std::shared_ptr<TFramedTransport>  openFileTransport(const char* name, bool out) {
	int fd;
	if (out) {
		fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IXUSR);
	} else {
		fd = open(name, O_RDONLY);
	}
	if (-1 == fd)
	{
		LOG(error) << ("ERROR: Open/create for write failed!\n");
		return nullptr;
	}

	std::shared_ptr<TFDTransport> file(new TFDTransport(fd));
	std::shared_ptr<TFramedTransport> transport(new TFramedTransport(file));
	return transport;
}

class TFileServer {
public:
	TFileServer(std::shared_ptr<TProcessor> processor, std::shared_ptr<TTransport> transportIn, std::shared_ptr<TProtocol> protocolIn, std::shared_ptr<TTransport> transportOut, std::shared_ptr<TProtocol> protocolOut) :
			 processor(processor), 
			 transportIn(transportIn),
			 protocolIn(protocolIn),
			 transportOut(transportOut),
			 protocolOut(protocolOut)
			 	{ }
	void serve() {
		bool print = true;
		for (;;) {
				try {
					processor.get()->process(protocolIn, protocolOut, NULL);
					print = true;
				} catch (TTransportException& ttx) {
					if (ttx.getType() == TTransportException::TTransportExceptionType::END_OF_FILE) {
						if (print) LOG(info) << "ran out of data: " << ttx.what() << "\n";
						print = false;
						continue;
					}
					LOG(error) << "breaking: " << ttx.what();
					break;
				}
		}
	}
private:
		std::shared_ptr<TProcessor> processor;
		std::shared_ptr<TTransport> transportIn;
		std::shared_ptr<TProtocol> protocolIn;
		std::shared_ptr<TTransport> transportOut;
		std::shared_ptr<TProtocol> protocolOut;
};

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
