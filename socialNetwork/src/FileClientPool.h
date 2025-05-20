#ifndef SOCIAL_NETWORK_MICROSERVICES_FILE_CLIENTPOOL_H
#define SOCIAL_NETWORK_MICROSERVICES_FILE_CLIENTPOOL_H

#include <vector>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <string>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace social_network {
using json = nlohmann::json;

template<class TClient>
class FileClientPool {
 public:
  FileClientPool(const std::string &client_type, const std::string &addr, 
      const std::string &filename, int port, int min_size, int max_size, int timeout_ms, int keepalive_ms,
      const json &config_json);
  ~FileClientPool();

  FileClientPool(const FileClientPool&) = delete;
  FileClientPool& operator=(const FileClientPool&) = delete;
  FileClientPool(FileClientPool&&) = default;
  FileClientPool& operator=(FileClientPool&&) = default;

  TClient * Pop();
  void Push(TClient *);
  void Keepalive(TClient *);
  void Remove(TClient *);

 private:
  TClient *_client;
  std::string _addr;
  std::string _client_type;
  std::string _filename;
  int _min_pool_size{};
  int _max_pool_size{};
  int _curr_pool_size{};
  int _timeout_ms;
  int _keepalive_ms;
  const json *_config_json;

};

template<class TClient>
FileClientPool<TClient>::FileClientPool(const std::string &client_type,
    const std::string &addr, const std::string &filename, int port, int min_pool_size,
    int max_pool_size, int timeout_ms, int keepalive_ms,
    const json &config_json) {
  _addr = addr;
  _filename = filename;
  _min_pool_size = min_pool_size;
  _max_pool_size = max_pool_size;
  _timeout_ms = timeout_ms;
  _client_type = client_type;
  _keepalive_ms = keepalive_ms;
  _config_json = &config_json;

  _client = new TClient(addr, filename, port, keepalive_ms, config_json);
}

template<class TClient>
FileClientPool<TClient>::~FileClientPool() {
  delete _client;
}

template<class TClient>
TClient * FileClientPool<TClient>::Pop() {
  TClient * client = _client;
  if (client) {
    try {
      client->Connect();
    } catch (...) {
      LOG(error) << "Failed to connect " + _client_type;
      Remove(client);
      throw;
    }
  }
  return client;
}

template<class TClient>
void FileClientPool<TClient>::Push(TClient *client) {
}

template<class TClient>
void FileClientPool<TClient>::Remove(TClient *client) {
}

template<class TClient>
void FileClientPool<TClient>::Keepalive(TClient *client) {
  // long curr_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
  //         std::chrono::system_clock::now().time_since_epoch()).count();
  // if (curr_timestamp - client->_connect_timestamp > client->_keepalive_ms) {
  //   Remove(client);
  // } else {
  //   Push(client);
  // }
}

} // namespace social_network


#endif //SOCIAL_NETWORK_MICROSERVICES_CLIENTPOOL_H
