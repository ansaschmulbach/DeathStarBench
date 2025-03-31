#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../dump_file_server.h"
#include "../utils.h"
#include "../utils_thrift.h"
#include "TextHandler.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::server::TSimpleServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using apache::thrift::transport::TFDTransport;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();
  // SetUpTracer("/data/sanchez/users/ansa/DSB/socialNetwork/config/jaeger-config.yml", "text-service");

  json config_json;
  if (load_config_file("//data/sanchez/users/ansa/DSB/socialNetwork/config/service-config.json", &config_json) == 0) {
    int port = config_json["text-service"]["port"];

    std::string url_addr = config_json["url-shorten-service"]["addr"];
    int url_port = config_json["url-shorten-service"]["port"];
    int url_conns = config_json["url-shorten-service"]["connections"];
    int url_timeout = config_json["url-shorten-service"]["timeout_ms"];
    int url_keepalive = config_json["url-shorten-service"]["keepalive_ms"];

    std::string user_mention_addr = config_json["user-mention-service"]["addr"];
    int user_mention_port = config_json["user-mention-service"]["port"];
    int user_mention_conns = config_json["user-mention-service"]["connections"];
    int user_mention_timeout =
        config_json["user-mention-service"]["timeout_ms"];
    int user_mention_keepalive =
        config_json["user-mention-service"]["keepalive_ms"];

    FileClientPool<FileClient<UrlShortenServiceClient>> url_client_pool(
        "url-shorten-service", url_addr, "//data/sanchez/users/ansa/DSB/socialNetwork/stream-" + std::to_string(url_port) + ".bin", 0, url_conns, url_timeout,
        url_keepalive, config_json);

    FileClientPool<FileClient<UserMentionServiceClient>> user_mention_pool(
        "user-mention-service", user_mention_addr, "//data/sanchez/users/ansa/DSB/socialNetwork/stream-" + std::to_string(user_mention_port) + ".bin", 0,
        user_mention_conns, user_mention_timeout, user_mention_keepalive, config_json);

    auto _transportOut = openFileTransport("out", true);
    if (!_transportOut) {
         LOG(error) << "could not open output trace file";
    }
    std::shared_ptr<TProtocol> _protocolOut(new TBinaryProtocol(_transportOut));

    auto _transportIn = openFileTransport("//data/sanchez/users/ansa/DSB/socialNetwork/stream-39328.bin", false);
    if (!_transportIn) {
         LOG(error) << "could not open input trace file";
    }
    std::shared_ptr<TProtocol> _protocolIn(new TBinaryProtocol(_transportIn));


    FileServer server(
        std::make_shared<TextServiceProcessor>(std::make_shared<TextHandler>(
            &url_client_pool, &user_mention_pool)),
	_transportIn,
	_protocolIn, 
	_transportOut,
	_protocolOut);

    LOG(info) << "Starting the text-service server...";
    server.serve();
  } else
    exit(EXIT_FAILURE);
}
