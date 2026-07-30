#ifndef SOCIAL_NETWORK_MICROSERVICES_LOGGER_H
#define SOCIAL_NETWORK_MICROSERVICES_LOGGER_H

#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <cstdlib>
#include <string.h>

namespace social_network {
#define __FILENAME__ \
    (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define LOG(severity) \
    BOOST_LOG_TRIVIAL(severity) << "(" << __FILENAME__ << ":" \
    << __LINE__ << ":" << __FUNCTION__ << ") "

// Per-request LOG(debug) calls (e.g. UniqueIdHandler::ComposeUniqueId) are
// already filtered out by the >= info default below -- BOOST_LOG_TRIVIAL
// short-circuits below the filter threshold, so that's cheap even at 100k
// requests. QUIET_LOGGING raises the bar further, to > fatal (i.e.
// everything, since nothing above fatal exists), for runs that want zero
// logging overhead at all, including the one-time startup LOG(info) lines --
// set it when perf-measuring so console I/O never shows up in the numbers.
void init_logger() {
  boost::log::register_simple_formatter_factory
      <boost::log::trivial::severity_level, char>("Severity");
  boost::log::add_common_attributes();
  boost::log::add_console_log(
      std::cerr, boost::log::keywords::format =
          "[%TimeStamp%] <%Severity%>: %Message%");
  bool quiet = std::getenv("QUIET_LOGGING") != nullptr;
  boost::log::core::get()->set_filter (
      boost::log::trivial::severity >= (quiet ? boost::log::trivial::fatal + 1
                                               : boost::log::trivial::info)
  );
}


} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_LOGGER_H
