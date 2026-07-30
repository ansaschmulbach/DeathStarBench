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
#include <sys/mman.h>
#include <sys/stat.h>
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
using apache::thrift::transport::TBufferedTransport;
using apache::thrift::transport::TFDTransport;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TMemoryBuffer;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  MaybeJoinGhostEnclave();
  MaybeAnnounceAndDelay("uid");
  init_logger();

  // Hardcoded: was previously derived from config/service-config.json's
  // "netif" via GetMachineId()'s MAC-address lookup, which only worked on
  // hosts with that specific interface name. Any fixed value works just as
  // well here since we only need it to be stable across a run, not tied to
  // real hardware.
  std::string machine_id = "2d4";
  LOG(info) << "machine_id = " << machine_id;

  std::mutex thread_lock;

  const char *trace_file_env = std::getenv("TRACE_FILE");
  std::string trace_file = trace_file_env ? trace_file_env : "trace-unique-id-service";

  // TRACE_WRAP (env var): how to wrap the input trace file's transport.
  // A/B'd against userspace-scheduling-prototype's file_server (never used
  // framing) -- framing overhead alone accounted for a ~2.8x cycle-count
  // increase on this same workload. See the NOTE on openFileTransport() in
  // utils_thrift.h for the numbers.
  //   framed (default)          FD -> TFramedTransport -> protocol
  //                              (production behavior)
  //   unframed                   FD -> TBufferedTransport -> protocol
  //                              (needs an unframed TRACE_FILE)
  //   buffered_around_framed     FD -> TFramedTransport -> TBufferedTransport
  //                              -> protocol. TFramedTransport still talks
  //                              directly to the fd underneath it regardless
  //                              of what wraps it from outside, so this
  //                              should do nothing for syscall count.
  //   framed_around_buffered     FD -> TBufferedTransport -> TFramedTransport
  //                              -> protocol. TFramedTransport's small reads
  //                              now get satisfied from the buffer's
  //                              already-fetched chunks instead of hitting
  //                              the fd each time -- should combine real
  //                              framing with fewer syscalls.
  //   mmap_unframed               mmap(fd) -> TMemoryBuffer(OBSERVE) ->
  //                              protocol, zero read() syscalls after the one
  //                              mmap() call. TBinaryProtocol messages are
  //                              self-delimiting (readMessageBegin/T_STOP),
  //                              so no framing layer is needed when the whole
  //                              file is already addressable memory. Needs an
  //                              unframed TRACE_FILE.
  //   mmap_framed                 mmap(fd) -> TMemoryBuffer(OBSERVE) ->
  //                              TFramedTransport -> protocol. Same zero-
  //                              syscall read path, but keeps real framing on
  //                              the wire (needs a framed TRACE_FILE) to see
  //                              whether TFramedTransport's own in-memory
  //                              bookkeeping costs anything once syscalls are
  //                              off the table entirely.
  // framed/buffered_around_framed/framed_around_buffered/mmap_framed all need
  // a *framed* TRACE_FILE; unframed/mmap_unframed need the unframed one.
  const char *wrap_env = std::getenv("TRACE_WRAP");
  std::string wrap = wrap_env ? wrap_env : "framed";

  int trace_fd = open(trace_file.c_str(), O_RDONLY);
  if (trace_fd < 0) {
    LOG(error) << "could not open input trace file " << trace_file;
    exit(EXIT_FAILURE);
  }

  std::shared_ptr<TTransport> _transportIn;
  if (wrap == "mmap_unframed" || wrap == "mmap_framed") {
    struct stat st;
    if (fstat(trace_fd, &st) != 0) {
      LOG(error) << "could not fstat input trace file " << trace_file;
      exit(EXIT_FAILURE);
    }
    void *mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, trace_fd, 0);
    if (mapped == MAP_FAILED) {
      LOG(error) << "could not mmap input trace file " << trace_file;
      exit(EXIT_FAILURE);
    }
    close(trace_fd);  // mapping stays valid after the fd is closed.
    std::shared_ptr<TMemoryBuffer> mapped_buf(new TMemoryBuffer(
        reinterpret_cast<uint8_t *>(mapped), st.st_size, TMemoryBuffer::OBSERVE));
    if (wrap == "mmap_framed") {
      _transportIn.reset(new TFramedTransport(mapped_buf));
    } else {
      _transportIn = mapped_buf;
    }
  } else {
    std::shared_ptr<TFDTransport> _fileIn(new TFDTransport(trace_fd));
    if (wrap == "unframed") {
      _transportIn.reset(new TBufferedTransport(_fileIn, 2048));
    } else if (wrap == "buffered_around_framed") {
      std::shared_ptr<TTransport> framed(new TFramedTransport(_fileIn));
      _transportIn.reset(new TBufferedTransport(framed, 2048));
    } else if (wrap == "framed_around_buffered") {
      std::shared_ptr<TTransport> buffered(new TBufferedTransport(_fileIn, 2048));
      _transportIn.reset(new TFramedTransport(buffered));
    } else {
      _transportIn.reset(new TFramedTransport(_fileIn));
    }
  }
  std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));

  bool unframed_out = (wrap == "unframed" || wrap == "mmap_unframed");
  auto _memOut = openMemoryTransport();
  std::shared_ptr<TTransport> _transportOut =
      unframed_out ? std::static_pointer_cast<TTransport>(_memOut)
               : std::shared_ptr<TTransport>(new TFramedTransport(_memOut));
  std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));

  TFileServer server(
      std::make_shared<UniqueIdServiceProcessor>(
          std::make_shared<UniqueIdHandler>(&thread_lock, machine_id)),
			_transportIn, _protocolIn, _transportOut, _protocolOut, "uid"
		  );

  LOG(info) << "Starting the unique-id-service server ...";
  server.serve();

  dumpMemoryTransportToFile(_memOut, "out-unique-id-service");
}
