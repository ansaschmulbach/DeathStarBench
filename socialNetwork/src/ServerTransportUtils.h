#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_SERVER_TRANSPORT_UTILS_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_SERVER_TRANSPORT_UTILS_H_

#include "zsim_hooks.h"
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <thrift/transport/PlatformSocket.h>
#include <thrift/transport/TFDTransport.h>
#include <thrift/transport/TFileTransport.h>
#include <thrift/transport/TServerTransport.h>
#include <thrift/transport/TTransportException.h>

namespace social_network {

// using apache::thrift::transport::TFDTransport;
// using apache::thrift::transport::TFileTransport;
// using apache::thrift::transport::TFramedTransport;
// using apache::thrift::transport::TServerTransport;
using apache::thrift::transport::TTransport;
// using apache::thrift::transport::TTransportException;

bool isEOF(int fd) {
  char buffer;
  ssize_t bytesRead = read(fd, &buffer, 1); // Attempt to read one byte
  if (bytesRead == 0) {
    // EOF reached
    return true;
  } else if (bytesRead > 0) {
    // Successfully read a byte, reposition the file pointer
    lseek(fd, -1, SEEK_CUR); // Move file pointer back by 1 byte
    return false;
  } else {
    // Handle errors (e.g., EAGAIN or EINTR for non-blocking reads)
    if (errno == EAGAIN || errno == EINTR) {
      return false; // No data yet, but not EOF
    }
    perror("read"); // Log actual error
    return true;
  }
}

class TTransportPair : public TTransport {

private:
  std::shared_ptr<TTransport> readTransport;  // Transport for reading
  std::shared_ptr<TTransport> writeTransport; // Transport for writing

public:
  TTransportPair(std::shared_ptr<TTransport> read,
                 std::shared_ptr<TTransport> write)
      : readTransport(std::move(read)), writeTransport(std::move(write)) {}

  bool isOpen() const {
    return readTransport->isOpen() && writeTransport->isOpen();
  }

  void open() override {
    readTransport->open();
    writeTransport->open();
  }

  void close() override {
    // TODO:  don't close for now...
    readTransport->close();
    writeTransport->close();
  }

  uint32_t read_virt(uint8_t *buf, uint32_t len) override {
    // zsim_cache_reset_ctr();
    // zsim_cache_reset();
    zsim_br_pred_reset();
    return readTransport->read(buf, len);
  }

  void write_virt(const uint8_t *buf, uint32_t len) override {
    // zsim_cache_reset_ctr();
    // zsim_cache_reset();
    zsim_br_pred_reset();
    writeTransport->write(buf, len);
  }

  void flush() override { writeTransport->flush(); }
};

} // namespace social_network

#endif // SOCIAL_NETWORK_MICROSERVICES_SRC_SERVER_TRANSPORT_UTILS_H_
