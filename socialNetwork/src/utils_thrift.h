#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string>
#include <nlohmann/json.hpp>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TFDTransport.h>
#include <thrift/transport/TSSLSocket.h>
#include <thrift/transport/TSSLServerSocket.h>

#define MAX_REQS_TO_SERVE 99

namespace social_network{
using json = nlohmann::json;
using apache::thrift::transport::TServerSocket;
using apache::thrift::transport::TSSLServerSocket;
using apache::thrift::transport::TSSLSocketFactory;
using apache::thrift::transport::TFramedTransport;
using apache::thrift::transport::TFDTransport;
using apache::thrift::transport::TMemoryBuffer;
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
  // return std::make_shared<TServerSocket>("/data/sanchez/users/ansa/uds/dsb-sock-" + std::to_string(port));
  return std::make_shared<TServerSocket>(address, port);
};

std::shared_ptr<TFramedTransport>  openFileTransport(const char* name, bool out) {
	int fd;
	int mmap_perms;
	std::shared_ptr<TFramedTransport> transport;
	if (out) {
		fd = open(name, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR | S_IXUSR);
		mmap_perms = PROT_READ | PROT_WRITE;
		if (-1 == fd) {
			LOG(error) << ("Could not open output file");
			return nullptr;
		}
		std::shared_ptr<TFDTransport> file(new TFDTransport(fd));
		transport = std::make_shared<TFramedTransport>(file);
	} else {
		fd = open(name, O_RDONLY);
		mmap_perms = PROT_READ;
		if (-1 == fd) {
			LOG(error) << ("Could not open input file");
			return nullptr;
		}

		struct stat fileInfo;
		if (fstat(fd, &fileInfo) == -1) {
			LOG(error) << ("Error getting file size");
			close(fd);
			return nullptr;
		}
		
		void* fileMemory = mmap(nullptr, fileInfo.st_size, mmap_perms, MAP_PRIVATE, fd, 0);
		if (fileMemory == MAP_FAILED) {
			LOG(error) << "file size is: " << fileInfo.st_size;
			LOG(error) << "fd is: " << fd;
			perror("Could not memory map file");
			close(fd);
			return nullptr;
		}
		
		
		std::shared_ptr<TMemoryBuffer> file(new TMemoryBuffer(static_cast<uint8_t*>(fileMemory), fileInfo.st_size));
		transport = std::make_shared<TFramedTransport>(file);
	}
	
	return transport;
}


} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
