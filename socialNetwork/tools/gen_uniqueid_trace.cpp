// Same idea as gen_media_trace.cpp, for UniqueIdService's ComposeUniqueId.
//
// Build (from socialNetwork/):
//   g++ -std=c++14 -O2 -I gen-cpp tools/gen_uniqueid_trace.cpp \
//       gen-cpp/UniqueIdService.cpp gen-cpp/ComposePostService.cpp \
//       gen-cpp/social_network_types.cpp -lthrift -lpthread \
//       -o gen_uniqueid_trace
//
// Usage: gen_uniqueid_trace <output_file> <num_requests>
// Then point UniqueIdService at it: TRACE_FILE=<output_file> ./UniqueIdService
//
// Writes UNFRAMED output (TBufferedTransport, not TFramedTransport) --
// matches UniqueIdService.cpp's default TRACE_WRAP=mmap_unframed read path
// (see the NOTE on openFileTransport() in ../src/utils_thrift.h). TBinaryProtocol
// messages are self-delimiting, so no length-prefix framing is needed.

#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <string>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TFDTransport.h>

#include "UniqueIdService.h"

using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::protocol::TProtocol;
using apache::thrift::transport::TBufferedTransport;
using apache::thrift::transport::TFDTransport;
using apache::thrift::transport::TTransport;

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <output_file> <num_requests>\n", argv[0]);
    return 1;
  }
  std::string output_path = argv[1];
  int num_requests = atoi(argv[2]);

  int fd = open(output_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd < 0) {
    perror("open output_path");
    return 1;
  }
  std::shared_ptr<TFDTransport> file(new TFDTransport(fd));
  std::shared_ptr<TBufferedTransport> transport(new TBufferedTransport(file));
  std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
  transport->open();

  social_network::UniqueIdServiceClient client(protocol);

  for (int i = 0; i < num_requests; i++) {
    int64_t req_id = 2000000 + i;
    client.send_ComposeUniqueId(req_id, social_network::PostType::POST);

    if (i % 10000 == 0) printf("wrote %d/%d\n", i, num_requests);
  }
  transport->close();

  printf("wrote %d requests to %s\n", num_requests, output_path.c_str());
  return 0;
}
