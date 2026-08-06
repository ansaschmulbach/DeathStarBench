// Standalone generator: writes N Thrift-framed ComposeMedia requests,
// back to back, into a single file, using the repo's own generated
// MediaServiceClient stub so the bytes are byte-for-byte what a real client
// would have sent. MediaService.cpp's TFileServer reads this file end to
// end, calling processor->process() once per framed request until EOF.
//
// Build (from socialNetwork/):
//   g++ -std=c++14 -O2 -I gen-cpp tools/gen_media_trace.cpp \
//       gen-cpp/MediaService.cpp gen-cpp/social_network_types.cpp \
//       -lthrift -lpthread -o gen_media_trace
//
// Usage: gen_media_trace <output_file> <num_requests>
// Then point MediaService at it: TRACE_FILE=<output_file> ./MediaService
//
// Writes UNFRAMED output (TBufferedTransport, not TFramedTransport) --
// matches MediaService.cpp's default (via openFileTransport() in
// ../src/utils_thrift.h) mmap+TMemoryBuffer(OBSERVE) unframed read path.
// TBinaryProtocol messages are self-delimiting, so no length-prefix framing
// is needed.

#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <string>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TFDTransport.h>

#include "MediaService.h"

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

  social_network::MediaServiceClient client(protocol);

  for (int i = 0; i < num_requests; i++) {
    int64_t req_id = 1000 + i;
    std::vector<std::string> media_types = {"png"};
    std::vector<int64_t> media_ids = {5000 + i};
    std::map<std::string, std::string> carrier;
    client.send_ComposeMedia(req_id, media_types, media_ids, carrier);

    if (i % 10000 == 0) printf("wrote %d/%d\n", i, num_requests);
  }
  transport->close();

  printf("wrote %d requests to %s\n", num_requests, output_path.c_str());
  return 0;
}
