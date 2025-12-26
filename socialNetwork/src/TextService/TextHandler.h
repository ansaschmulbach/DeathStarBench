#ifndef SOCIAL_NETWORK_MICROSERVICES_TEXTHANDLER_H
#define SOCIAL_NETWORK_MICROSERVICES_TEXTHANDLER_H

#include <boost/log/trivial.hpp>
#include <future>
#include <iostream>
#include <regex>
#include <string>

#include "../../gen-cpp/TextService.h"
#include "../../gen-cpp/UrlShortenService.h"
#include "../../gen-cpp/UserMentionService.h"
#include "../ClientPool.h"
#include "../FileClientPool.h"
#include "../ThriftClient.h"
#include "../ThriftFileClient.h"
#include "../logger.h"
#include <thrift/TApplicationException.h>
#include <thrift/protocol/TProtocolException.h>
#include <thrift/transport/TTransportException.h>
// #include "../tracing.h"
#include "../zsim_hooks.h"

#include <chrono>
#include <thread>

using apache::thrift::TApplicationException;
using apache::thrift::protocol::TProtocolException;
using apache::thrift::transport::TTransportException;

namespace social_network {

class TextHandler : public TextServiceIf {
public:
  TextHandler(ClientPool<ThriftClient<UrlShortenServiceClient>> *,
              ClientPool<ThriftClient<UserMentionServiceClient>> *);
  ~TextHandler() override = default;

  void ComposeText(TextServiceReturn &_return, int64_t, const std::string &,
                   const std::map<std::string, std::string> &) override;

  void Exit() override;

private:
  ClientPool<ThriftClient<UrlShortenServiceClient>> *_url_client_pool;
  ClientPool<ThriftClient<UserMentionServiceClient>> *_user_mention_client_pool;
  int req_serve_count = 0;
  bool obey_req_serve_max = true;
  // const int req_serve_max = 99;
};

TextHandler::TextHandler(
    ClientPool<ThriftClient<UrlShortenServiceClient>> *url_client_pool,
    ClientPool<ThriftClient<UserMentionServiceClient>>
        *user_mention_client_pool) {
  _url_client_pool = url_client_pool;
  _user_mention_client_pool = user_mention_client_pool;
}

void TextHandler::ComposeText(
    TextServiceReturn &_return, int64_t req_id, const std::string &text,
    const std::map<std::string, std::string> &carrier) {

  if (obey_req_serve_max && req_serve_count == MAX_REQS_TO_SERVE) {
    // exit(0);
  } else if (req_serve_count == MAX_REQS_TO_SERVE) {
    zsim_roi_end();
  } else if (req_serve_count == 0) {
    zsim_roi_begin();
  }

  zsim_cache_reset();
  zsim_br_pred_reset();
  zsim_request_begin();
  zsim_heartbeat();
  LOG(info) << "requests served: " << req_serve_count;
  req_serve_count++;
  // LOG(info) << "received compose text request";
  // zsim_cache_reset();
  // zsim_br_pred_reset();

  // Initialize a span
  // TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  // TextMapWriter writer(writer_text_map);
  // auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  // auto span = opentracing::Tracer::Global()->StartSpan(
  //     "compose_text_server", {opentracing::ChildOf(parent_span->get())});
  // opentracing::Tracer::Global()->Inject(span->context(), writer);

  std::vector<std::string> mention_usernames;
  std::smatch m;
  std::regex e("@[a-zA-Z0-9-_]+");
  auto s = text;
  while (std::regex_search(s, m, e)) {
    auto user_mention = m.str();
    user_mention = user_mention.substr(1, user_mention.length());
    mention_usernames.emplace_back(user_mention);
    s = m.suffix().str();
    // zsim_cache_reset();
  }

  std::vector<std::string> urls;
  e = "(http://|https://)([a-zA-Z0-9_!~*'().&=+$%-]+)";
  s = text;
  while (std::regex_search(s, m, e)) {
    auto url = m.str();
    urls.emplace_back(url);
    s = m.suffix().str();
    // zsim_cache_reset();
  }

  // auto shortened_urls_future = std::async(std::launch::async, [&]() {
  // auto url_span = opentracing::Tracer::Global()->StartSpan(
  //     "compose_urls_client", {opentracing::ChildOf(&span->context())});

  std::map<std::string, std::string> url_writer_text_map;
  // TextMapWriter url_writer(url_writer_text_map);
  // opentracing::Tracer::Global()->Inject(url_span->context(), url_writer);

  auto url_client_wrapper = _url_client_pool->Pop();
  if (!url_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to url-shorten-service";
    throw se;
  }
  std::vector<Url> _return_urls;
  auto url_client = url_client_wrapper->GetClient();
  try {
    url_client->ComposeUrls(_return_urls, req_id, urls, url_writer_text_map);
  } catch (const TTransportException &e) {
    LOG(error) << "Failed to upload urls to url-shorten-service";
    std::cerr << "Transport error: " << e.what() << std::endl;
    _url_client_pool->Remove(url_client_wrapper);
    throw e;
  } catch (const TProtocolException &e) {
    LOG(error) << "Failed to upload urls to url-shorten-service";
    std::cerr << "Protocol error: " << e.what() << std::endl;
    _url_client_pool->Remove(url_client_wrapper);
    throw e;
  } catch (const TApplicationException &e) {
    LOG(error) << "Failed to upload urls to url-shorten-service";
    std::cerr << "Application error: " << e.what() << std::endl;
    _url_client_pool->Remove(url_client_wrapper);
    throw e;
  } catch (const TException &e) {
    LOG(error) << "Failed to upload urls to url-shorten-service";
    std::cerr << "Generic Thrift error: " << e.what() << std::endl;
    _url_client_pool->Remove(url_client_wrapper);
    throw e;
  } catch (const std::exception &e) {
    LOG(error) << "Failed to upload urls to url-shorten-service";
    std::cerr << "Other std::exception: " << e.what() << std::endl;
    _url_client_pool->Remove(url_client_wrapper);
    throw e;
  } catch (...) {
    LOG(error) << "Failed to upload urls to url-shorten-service";
    std::cerr << "Unknown exception caught during Thrift call" << std::endl;
    _url_client_pool->Remove(url_client_wrapper);
    throw;
  }
  // LOG(info) << "finished url upload";
  _url_client_pool->Keepalive(url_client_wrapper);
  // return _return_urls;
  // });

  // auto user_mention_future = std::async(std::launch::async, [&]() {
  // auto user_mention_span = opentracing::Tracer::Global()->StartSpan(
  //     "compose_user_mentions_client",
  //     {opentracing::ChildOf(&span->context())});

  std::map<std::string, std::string> user_mention_writer_text_map;
  // TextMapWriter user_mention_writer(user_mention_writer_text_map);
  // opentracing::Tracer::Global()->Inject(user_mention_span->context(),
  //                                       user_mention_writer);

  auto user_mention_client_wrapper = _user_mention_client_pool->Pop();
  if (!user_mention_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to user-mention-service";
    throw se;
  }
  std::vector<UserMention> _return_user_mentions;
  auto user_mention_client = user_mention_client_wrapper->GetClient();
  try {
    user_mention_client->ComposeUserMentions(_return_user_mentions, req_id,
                                             mention_usernames,
                                             user_mention_writer_text_map);
  } catch (const TTransportException &e) {
    LOG(error) << "Failed to upload user_mentions to user-mention-service";
    std::cerr << "Transport error: " << e.what() << std::endl;
    _user_mention_client_pool->Remove(user_mention_client_wrapper);
    throw e;
  } catch (const TProtocolException &e) {
    LOG(error) << "Failed to upload user_mentions to user-mention-service";
    std::cerr << "Protocol error: " << e.what() << std::endl;
    _user_mention_client_pool->Remove(user_mention_client_wrapper);
    throw e;
  } catch (const TApplicationException &e) {
    LOG(error) << "Failed to upload user_mentions to user-mention-service";
    std::cerr << "Application error: " << e.what() << std::endl;
    _user_mention_client_pool->Remove(user_mention_client_wrapper);
    throw e;
  } catch (const TException &e) {
    LOG(error) << "Failed to upload user_mentions to user-mention-service";
    std::cerr << "Generic Thrift error: " << e.what() << std::endl;
    _user_mention_client_pool->Remove(user_mention_client_wrapper);
    throw e;
  } catch (const std::exception &e) {
    LOG(error) << "Failed to upload user_mentions to user-mention-service";
    std::cerr << "Other std::exception: " << e.what() << std::endl;
    _user_mention_client_pool->Remove(user_mention_client_wrapper);
    throw e;
  } catch (...) {
    LOG(error) << "Failed to upload user_mentions to user-mention-service";
    std::cerr << "Unknown exception caught during Thrift call" << std::endl;
    _user_mention_client_pool->Remove(user_mention_client_wrapper);
    throw;
  }

  // LOG(info) << "finished user mentions";

  _user_mention_client_pool->Keepalive(user_mention_client_wrapper);
  // return _return_user_mentions;
  // });

  std::vector<Url> target_urls;
  target_urls = _return_urls;
  // try {
  //   target_urls = shortened_urls_future.get();
  // } catch (...) {
  //   LOG(error) << "Failed to get shortened urls from url-shorten-service";
  //   throw;
  // }

  std::vector<UserMention> user_mentions;
  user_mentions = _return_user_mentions;
  // try {
  //   user_mentions = user_mention_future.get();
  // } catch (...) {
  //   LOG(error) << "Failed to upload user mentions to user-mention-service";
  //   throw;
  // }

  std::string updated_text;
  if (!urls.empty()) {
    s = text;
    int idx = 0;
    while (std::regex_search(s, m, e)) {
      auto url = m.str();
      urls.emplace_back(url);
      updated_text += m.prefix().str() + target_urls[idx].shortened_url;
      s = m.suffix().str();
      idx++;
    }
  } else {
    updated_text = text;
  }

  _return.user_mentions = user_mentions;
  _return.text = updated_text;
  _return.urls = target_urls;
  // span->Finish();
  // zsim_heartbeat();
  // LOG(info) << "returning";
  zsim_request_end();
}

void TextHandler::Exit() {
  auto url_client_wrapper = _url_client_pool->Pop();
  if (!url_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to url-shorten-service";
    throw se;
  }
  auto url_client = url_client_wrapper->GetClient();
  url_client->Exit();

  auto user_mention_client_wrapper = _user_mention_client_pool->Pop();
  if (!user_mention_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to user-mention-service";
    throw se;
  }
  auto user_mention_client = user_mention_client_wrapper->GetClient();
  user_mention_client->Exit();

  LOG(info) << "Exiting...";

  exit(0);
}

class TextServiceAsyncHandler : public TextServiceCobSvIf {
public:
  TextServiceAsyncHandler(
      ClientPool<ThriftClient<UrlShortenServiceClient>> *url_client_pool,
      ClientPool<ThriftClient<UserMentionServiceClient>>
          *user_mention_client_pool) {
    syncHandler_ = std::auto_ptr<TextHandler>(
        new TextHandler(url_client_pool, user_mention_client_pool));
    // Your initialization goes here
  }
  virtual ~TextServiceAsyncHandler();

  void ComposeText(
      ::apache::thrift::stdcxx::function<void(TextServiceReturn const &_return)>
          cob,
      ::apache::thrift::stdcxx::function<
          void(::apache::thrift::TDelayedException *_throw)> /* exn_cob */,
      const int64_t req_id, const std::string &text,
      const std::map<std::string, std::string> &carrier) {
    TextServiceReturn _return;
    syncHandler_->ComposeText(_return, req_id, text, carrier);
    return cob(_return);
  }

  void Exit(::apache::thrift::stdcxx::function<void()> cob) {
    syncHandler_->Exit();
    return cob();
  }

protected:
  std::auto_ptr<TextHandler> syncHandler_;
};

} // namespace social_network

#endif // SOCIAL_NETWORK_MICROSERVICES_TEXTHANDLER_H
