// Same idea as gen_media_trace.cpp, for UniqueIdService's ComposeUniqueId.
//
// Build (from socialNetwork/):
//   g++ -std=c++14 -O2 -I gen-cpp tools/gen_uniqueid_trace.cpp \
//       gen-cpp/UniqueIdService.cpp gen-cpp/ComposePostService.cpp \
//       gen-cpp/social_network_types.cpp -lthrift -lpthread \
//       -o gen_uniqueid_trace
//
// Usage: gen_uniqueid_trace <traces_dir> <report_xml_out> <num_requests>
// Output report_xml_out replaces UniqueIdService.cpp's hardcoded
// /social-network-microservices/report.xml; traces_dir's contents go in the
// run directory's traces/.

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
    snprintf(filename, sizeof(filename), "synth.uniqueid.%05d", i);
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

    social_network::UniqueIdServiceClient client(protocol);

    int64_t req_id = 2000000 + i;
    std::map<std::string, std::string> carrier;
    client.send_ComposeUniqueId(req_id, social_network::PostType::POST, carrier);
    transport->close();

    int sec = i;  // seconds apart; irrelevant now that sleep_for is removed,
                  // but Client::preprocessXml still parses these fields.
    char startime[64], endtime[64];
    snprintf(startime, sizeof(startime), "2026-07-30T%02d:%02d:%02d.000000Z",
             sec / 3600, (sec / 60) % 60, sec % 60);
    snprintf(endtime, sizeof(endtime), "2026-07-30T%02d:%02d:%02d.500000Z",
             sec / 3600, (sec / 60) % 60, sec % 60);

    fprintf(xml,
        "    <fileobject>\n"
        "      <filename>%s</filename>\n"
        "      <tcpflow startime='%s' endtime='%s' mac_daddr='02:42:ac:12:00:18' "
        "mac_saddr='02:42:ac:12:00:14' family='2' src_ipn='172.18.0.20' "
        "dst_ipn='172.18.0.24' srcport='%d' dstport='9090' packets='1' len='0' />\n"
        "    </fileobject>\n",
        filename, startime, endtime, 50000 + (i % 15000));

    if (i % 1000 == 0) printf("wrote %d/%d\n", i, num_requests);
  }

  fprintf(xml, "  </configuration>\n");
  fprintf(xml, "</dfxml>\n");
  fclose(xml);
  printf("wrote %s\n", report_path.c_str());
  return 0;
}
