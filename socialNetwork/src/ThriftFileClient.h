#ifndef SOCIAL_NETWORK_MICROSERVICES_THRIFTCLIENT_H
#define SOCIAL_NETWORK_MICROSERVICES_THRIFTCLIENT_H

#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <boost/log/trivial.hpp>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TSSLSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/stdcxx.h>
#include <nlohmann/json.hpp>
#include "logger.h"
#include "GenericClient.h"
#include "utils_thrift.h"


namespace social_network {

using apache::thrift::protocol::TProtocol;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TFramedTransport;
using apache::thrift::transport::TSocket;
using apache::thrift::transport::TSSLSocketFactory;
using apache::thrift::transport::TTransport;
using apache::thrift::TException;
using json = nlohmann::json;

template<class TThriftClient>
class FileClient : public GenericClient {
 public:
  FileClient(const std::string &addr, const std::string &filename);
  FileClient(const std::string &addr, const std::string &filename, int keepalive_ms, const json &config_json);

  FileClient(const FileClient &) = delete;
  FileClient &operator=(const FileClient &) = delete;
  FileClient(FileClient<TThriftClient> &&) = default;
  FileClient &operator=(FileClient &&) = default;

  ~FileClient() override;

  TThriftClient *GetClient() const;

  void Connect() override;
  void Disconnect() override;
  bool IsConnected() override;

 private:
  TThriftClient *_client;

  std::shared_ptr<TTransport> _transportIn;
  std::shared_ptr<TProtocol> _protocolIn;
  std::shared_ptr<TTransport> _transportOut;
  std::shared_ptr<TProtocol> _protocolOut;
};

template<class TThriftClient>
FileClient<TThriftClient>::FileClient(
    const std::string &addr, const std::string &filename) {
  _addr = addr;

	_transportIn = openFileTransport(filename.c_str(), false);
	if (!_transportIn) {
		LOG(error) << "could not open input trace file";
	}
  _protocolIn = std::shared_ptr<TProtocol>(new TBinaryProtocol(_transportIn));
	_transportOut = openFileTransport("out", true);
	if (!_transportOut) {
		LOG(error) << "could not open output trace file";
	}
  _protocolOut = std::shared_ptr<TProtocol>(new TBinaryProtocol(_transportOut));
  _client = new TThriftClient(_protocolIn, _protocolOut);
  _connect_timestamp = 0;
  _keepalive_ms = 0;
}

template <class TThriftClient>
FileClient<TThriftClient>::FileClient(
    const std::string &addr, const std::string &filename, int keepalive_ms, const json &config_json) {
  _addr = addr;
	_transportIn = openFileTransport(filename.c_str(), false);
	if (!_transportIn) {
		LOG(error) << "could not open input trace file";
	}
  _protocolIn = std::shared_ptr<TProtocol>(new TBinaryProtocol(_transportIn));
	_transportOut = openFileTransport("out", true);
	if (!_transportOut) {
		LOG(error) << "could not open output trace file";
	}
  _protocolOut = std::shared_ptr<TProtocol>(new TBinaryProtocol(_transportOut));
  _client = new TThriftClient(_protocolIn, _protocolOut);

  _connect_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  _keepalive_ms = keepalive_ms;
}

template<class TThriftClient>
FileClient<TThriftClient>::~FileClient() {
  Disconnect();
  delete _client;
}

template<class TThriftClient>
TThriftClient *FileClient<TThriftClient>::GetClient() const {
  return _client;
}

template<class TThriftClient>
bool FileClient<TThriftClient>::IsConnected() {
  return _transportIn->isOpen() && _transportOut->isOpen();
}

template<class TThriftClient>
void FileClient<TThriftClient>::Connect() {
  if (!IsConnected()) {
    try {
      _transportIn->open();
      _transportOut->open();
    } catch (TException &tx) {
      throw tx;
    }
  }
}

template<class TThriftClient>
void FileClient<TThriftClient>::Disconnect() {
  if (IsConnected()) {
    try {
      _transportOut->close();
      _transportIn->close();
    } catch (TException &tx) {
      throw tx;
    }
  }
}

} // namespace social_network


#endif //SOCIAL_NETWORK_MICROSERVICES_THRIFTCLIENT_H
