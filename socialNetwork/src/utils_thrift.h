#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_

#include <string>
#include <nlohmann/json.hpp>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSSLSocket.h>
#include <thrift/transport/TSSLServerSocket.h>

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TFileTransport.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TFDTransport.h>

namespace social_network{
using json = nlohmann::json;
using apache::thrift::transport::TServerSocket;
using apache::thrift::transport::TSSLServerSocket;
using apache::thrift::transport::TSSLSocketFactory;
using apache::thrift::transport::TBufferedTransport;
using apache::thrift::transport::TFDTransport;

std::shared_ptr<TBufferedTransport>  openFileTransport(const char* name, bool out) {
	int fd;
	if (out) {
		fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IXUSR);
	} else {
		fd = open(name, O_RDONLY);
	}
	if (-1 == fd)
	{
		printf("ERROR: Open/create for write failed!\n");
		return nullptr;
	}

	std::shared_ptr<TFDTransport> file(new TFDTransport(fd));
	std::shared_ptr<TBufferedTransport> transport(new TBufferedTransport(file));
	return transport;
}

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

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
