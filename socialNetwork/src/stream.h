#ifndef SOCIAL_NETWORK_MICROSERVICES_STREAM_H
#define SOCIAL_NETWORK_MICROSERVICES_STREAM_H

#include <string>
#include <ctime>
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <list>
#include <cassert>

#include "tinyxml2.h"

using std::chrono::system_clock;

namespace social_network{

class Stream {
public:
	std::string filename;
	std::chrono::duration<double> delay_time;
	system_clock::time_point start_timestamp;
	system_clock::time_point end_timestamp;
	const char *src_mac;
	const char *dst_mac;
	const char *src_ipn;
	const char *dst_ipn;
	uint64_t srcport;
	uint64_t dstport;
	uint32_t packets;
	uint32_t len;
};

class Client {
public:
	std::string streamData();
	Stream getNextStream();

	Client(uint64_t port, const char* filename);
private: 
	uint32_t _port;
	system_clock::time_point start_timestamp;
	std::list<Stream> streams;

	void setStartTimestamp();
	void assignDuration();
	void insertOrderedIntoList(Stream &stream);
	void preprocessXml(const char* filename);
};

}

#endif //SOCIAL_NETWORK_MICROSERVICES_STREAM_H
