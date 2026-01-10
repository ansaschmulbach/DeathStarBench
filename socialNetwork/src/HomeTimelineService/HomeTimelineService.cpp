#include <signal.h>
#include <thrift/async/TAsyncProtocolProcessor.h>
#include <thrift/async/TEvhttpServer.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include <boost/program_options.hpp>

#include "../ClientPool.h"
#include "../logger.h"
// #include "../tracing.h"
#include "../utils.h"
#include "../utils_redis.h"
#include "../utils_thrift.h"
#include "HomeTimelineHandler.h"

using apache::thrift::async::TAsyncBufferProcessor;
using apache::thrift::async::TAsyncProtocolProcessor;
using apache::thrift::async::TEvhttpServer;
using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TSimpleServer;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();

  // Command line options
  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()("help", "produce help message")(
      "redis-cluster",
      po::value<bool>()->default_value(false)->implicit_value(true),
      "Enable redis cluster mode");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }

  bool redis_cluster_flag = false;
  if (vm.count("redis-cluster")) {
    if (vm["redis-cluster"].as<bool>()) {
      redis_cluster_flag = true;
    }
  }

  // SetUpTracer("/data/sanchez/users/ansa/DSB/socialNetwork/config/jaeger-config.yml",
  // "home-timeline-service");

  json config_json;
  if (load_config_file("config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  int port = config_json["home-timeline-service"]["port"];
  int redis_cluster_config_flag =
      config_json["home-timeline-redis"]["use_cluster"];

  int redis_replica_config_flag =
      config_json["home-timeline-redis"]["use_replica"];

  int post_storage_port = config_json["post-storage-service"]["port"];
  std::string post_storage_addr = config_json["post-storage-service"]["addr"];
  int post_storage_conns = config_json["post-storage-service"]["connections"];
  int post_storage_timeout = config_json["post-storage-service"]["timeout_ms"];
  int post_storage_keepalive =
      config_json["post-storage-service"]["keepalive_ms"];

  int social_graph_port = config_json["social-graph-service"]["port"];
  std::string social_graph_addr = config_json["social-graph-service"]["addr"];
  int social_graph_conns = config_json["social-graph-service"]["connections"];
  int social_graph_timeout = config_json["social-graph-service"]["timeout_ms"];
  int social_graph_keepalive =
      config_json["social-graph-service"]["keepalive_ms"];

  if (redis_replica_config_flag &&
      (redis_cluster_config_flag || redis_cluster_flag)) {
    LOG(error) << "Can't start service when Redis Cluster and Redis Replica "
                  "are enabled at the same time";
    exit(EXIT_FAILURE);
  }

  ClientPool<ThriftClient<PostStorageServiceClient>> post_storage_client_pool(
      "post-storage-client", post_storage_addr, post_storage_port, 0,
      post_storage_conns, post_storage_timeout, post_storage_keepalive,
      config_json);

  ClientPool<ThriftClient<SocialGraphServiceClient>> social_graph_client_pool(
      "social-graph-client", social_graph_addr, social_graph_port, 0,
      social_graph_conns, social_graph_timeout, social_graph_keepalive,
      config_json);

  if (redis_replica_config_flag) {
    Redis redis_replica_client_pool =
        init_redis_replica_client_pool(config_json, "redis-replica");
    Redis redis_primary_client_pool =
        init_redis_replica_client_pool(config_json, "redis-primary");

    std::shared_ptr<apache::thrift::async::TAsyncProcessor> processorAsync =
        std::make_shared<HomeTimelineServiceAsyncProcessor>(
            std::make_shared<HomeTimelineServiceAsyncHandler>(
                &redis_replica_client_pool, &redis_primary_client_pool,
                &post_storage_client_pool, &social_graph_client_pool));

    std::shared_ptr<apache::thrift::async::TAsyncBufferProcessor>
        bufferProcessor(new TAsyncProtocolProcessor(
            processorAsync, std::make_shared<TBinaryProtocolFactory>()));

    TEvhttpServer server(bufferProcessor, port);
    LOG(info) << "Starting the home-timeline-service server with replicated "
                 "Redis support...";
    server.serve();

  }

  else if (redis_cluster_flag || redis_cluster_config_flag) {
    RedisCluster redis_cluster_client_pool =
        init_redis_cluster_client_pool(config_json, "home-timeline");

    std::shared_ptr<apache::thrift::async::TAsyncProcessor> processorAsync =
        std::make_shared<HomeTimelineServiceAsyncProcessor>(
            std::make_shared<HomeTimelineServiceAsyncHandler>(
                &redis_cluster_client_pool, &post_storage_client_pool,
                &social_graph_client_pool));

    std::shared_ptr<apache::thrift::async::TAsyncBufferProcessor>
        bufferProcessor(new TAsyncProtocolProcessor(
            processorAsync, std::make_shared<TBinaryProtocolFactory>()));

    TEvhttpServer server(bufferProcessor, port);

    LOG(info) << "Starting the home-timeline-service server with Redis Cluster "
                 "support...";
    server.serve();
  } else {
    Redis redis_client_pool =
        init_redis_client_pool(config_json, "home-timeline");

    std::shared_ptr<apache::thrift::async::TAsyncProcessor> processorAsync =
        std::make_shared<HomeTimelineServiceAsyncProcessor>(
            std::make_shared<HomeTimelineServiceAsyncHandler>(
                &redis_client_pool, &post_storage_client_pool,
                &social_graph_client_pool));

    std::shared_ptr<apache::thrift::async::TAsyncBufferProcessor>
        bufferProcessor(new TAsyncProtocolProcessor(
            processorAsync, std::make_shared<TBinaryProtocolFactory>()));

    TEvhttpServer server(bufferProcessor, port);

    LOG(info) << "Starting the home-timeline-service server...";
    server.serve();
  }
}
