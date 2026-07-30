// Standalone generator: writes N tiny files, each containing exactly one
// Thrift-framed ComposeMedia request, using the repo's own generated
// MediaServiceClient stub so the bytes are byte-for-byte what a real client
// would have sent. TcpDumpFileServer::serve() calls processor->process()
// exactly once per replayed file, so one request per file is what we want.
//
// Build (from socialNetwork/):
//   g++ -std=c++14 -O2 -I gen-cpp tools/gen_media_trace.cpp \
//       gen-cpp/MediaService.cpp gen-cpp/social_network_types.cpp \
//       -lthrift -lpthread -o gen_media_trace
//
// Usage: gen_media_trace <traces_dir> <report_xml_out> <num_requests>
// Then point MediaService.cpp's reportFilename/traces dir at the output, or
// copy into your run directory as report-media.xml + traces/.

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
using apache::thrift::transport::TFDTransport;
using apache::thrift::transport::TFramedTransport;
using apache::thrift::transport::TTransport;

int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <traces_dir> <report_xml_out> <num_requests>\n", argv[0]);
    return 1;
  }
  std::string traces_dir = argv[1];
  std::string report_path = argv[2];
  int num_requests = atoi(argv[3]);

  FILE* xml = fopen(report_path.c_str(), "w");
  if (!xml) {
    perror("fopen report_path");
    return 1;
  }
  fprintf(xml, "<?xml version='1.0' encoding='UTF-8'?>\n");
  fprintf(xml, "<dfxml xmloutputversion='1.0'>\n");
  fprintf(xml, "  <configuration>\n");
  fprintf(xml, "  </configuration>\n");
  fprintf(xml, "  <configuration>\n");

  for (int i = 0; i < num_requests; i++) {
    char filename[64];
    snprintf(filename, sizeof(filename), "synth.media.%03d", i);
    std::string full_path = traces_dir + "/" + filename;

    int fd = open(full_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
      perror("open trace file");
      return 1;
    }
    std::shared_ptr<TFDTransport> file(new TFDTransport(fd));
    std::shared_ptr<TFramedTransport> transport(new TFramedTransport(file));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    transport->open();

    social_network::MediaServiceClient client(protocol);

    int64_t req_id = 1000 + i;
    std::vector<std::string> media_types = {"png"};
    std::vector<int64_t> media_ids = {5000 + i};
    std::map<std::string, std::string> carrier;
    client.send_ComposeMedia(req_id, media_types, media_ids, carrier);
    transport->close();

    // Fabricated but monotonically increasing timestamps 1s apart; only the
    // relative ordering/spacing matters to Client::assignDuration().
    char startime[64], endtime[64];
    int sec = i * 5;
    snprintf(startime, sizeof(startime), "2026-07-30T00:%02d:%02d.000000Z", sec / 60, sec % 60);
    snprintf(endtime, sizeof(endtime), "2026-07-30T00:%02d:%02d.500000Z", sec / 60, sec % 60);

    fprintf(xml,
        "    <fileobject>\n"
        "      <filename>%s</filename>\n"
        "      <tcpflow startime='%s' endtime='%s' mac_daddr='02:42:ac:12:00:18' "
        "mac_saddr='02:42:ac:12:00:14' family='2' src_ipn='172.18.0.20' "
        "dst_ipn='172.18.0.25' srcport='%d' dstport='9090' packets='1' len='0' />\n"
        "    </fileobject>\n",
        filename, startime, endtime, 40000 + i);

    printf("wrote %s\n", full_path.c_str());
  }

  fprintf(xml, "  </configuration>\n");
  fprintf(xml, "</dfxml>\n");
  fclose(xml);
  printf("wrote %s\n", report_path.c_str());
  return 0;
}
