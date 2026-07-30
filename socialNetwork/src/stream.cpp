#include "stream.h"

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;
using tinyxml2::XMLNode;
using tinyxml2::XMLAttribute;
using tinyxml2::XMLError;

namespace social_network{

// Function to parse the datetime string
system_clock::time_point parseDatetime(const std::string& datetime_str) {
    std::tm tm = {};
    std::istringstream ss(datetime_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    // Handle fractional seconds
    std::chrono::microseconds us(0);
    if (ss.peek() == '.') {
        ss.ignore();
        std::string fractional_seconds;
        ss >> fractional_seconds;
        fractional_seconds.resize(6, '0'); // Ensure it has 6 digits for microseconds
        us = std::chrono::microseconds(std::stoi(fractional_seconds));
    }

	auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm)) + us;
    return tp;
}

void createStreamFromXml(Stream &stream, XMLElement *fileNode) {
	stream.filename = fileNode->FirstChildElement("filename")->GetText();
	XMLElement *tcpflowInfo = fileNode->FirstChildElement("tcpflow");

	const char* startTime;
	XMLError startTimeAttribute = tcpflowInfo->QueryStringAttribute("startime", &startTime);
	if (startTimeAttribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing start time attribute in xml file:" << startTimeAttribute;
		return;
	} 	
	stream.start_timestamp = parseDatetime(startTime);

	const char* endTime;
	XMLError endTimeAttribute = tcpflowInfo->QueryStringAttribute("endtime", &endTime);
	if (endTimeAttribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing end time attribute in xml file:" << endTimeAttribute;
		return;
	} 	
	stream.end_timestamp = parseDatetime(endTime);

	XMLError macSaddrAttribute = tcpflowInfo->QueryStringAttribute("mac_saddr", &stream.src_mac);
	if (macSaddrAttribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing src mac in xml file:" << macSaddrAttribute;
		return;
	} 	
	
	XMLError macDaddrAttribute = tcpflowInfo->QueryStringAttribute("mac_daddr", &stream.dst_mac);
	if (macDaddrAttribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing dst mac in xml file:" << macDaddrAttribute;
		return;
	} 	

	XMLError src_ipn_attribute = tcpflowInfo->QueryStringAttribute("src_ipn", &stream.src_ipn);
	if (src_ipn_attribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing src ipn in xml file:" << src_ipn_attribute;
		return;
	} 	

	XMLError dst_ipn_attribute = tcpflowInfo->QueryStringAttribute("dst_ipn", &stream.dst_ipn);
	if (dst_ipn_attribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing dst ipn in xml file:" << dst_ipn_attribute;
		return;
	} 	

	const char *srcport;
	XMLError srcport_attribute = tcpflowInfo->QueryStringAttribute("srcport", &srcport);
	if (srcport_attribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing src port in xml file:" << srcport_attribute;
		return;
	} 	
	stream.srcport = atoi(srcport);

	const char *dstport;
	XMLError dstport_attribute = tcpflowInfo->QueryStringAttribute("dstport", &dstport);
	if (dstport_attribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing dst port in xml file:" << dstport_attribute;
		return;
	} 	
	stream.dstport = atoi(dstport);

	const char *packets;
	XMLError packets_attribute = tcpflowInfo->QueryStringAttribute("packets", &packets);
	if (packets_attribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing packets in xml file:" << packets_attribute;
		return;
	} 	
	stream.packets = atoi(packets);

	const char *len;
	XMLError len_attribute = tcpflowInfo->QueryStringAttribute("len", &len);
	if (len_attribute != tinyxml2::XML_SUCCESS) {
		std::cout << "error in parsing len in xml file:" << len_attribute;
		return;
	} 	
	stream.len = atoi(len);
}

void Client::insertOrderedIntoList(Stream &stream) {
    auto it = streams.begin();
    while (it != streams.end() && (*it).start_timestamp < stream.start_timestamp) {
        ++it;
    }
    streams.insert(it, stream);
}

void Client::setStartTimestamp() {
	assert(streams.size() > 0);
	start_timestamp = streams.front().start_timestamp;
	std::time_t earliest_c = std::chrono::system_clock::to_time_t(start_timestamp);
}

void Client::assignDuration() {
	auto last = start_timestamp;
	for (auto &stream : streams) {
		stream.delay_time = stream.start_timestamp - last;
		last = stream.start_timestamp;
	}
}


void Client::preprocessXml(const char* filename) {
	XMLDocument doc;
	doc.LoadFile(filename);

	XMLElement *fileNode = doc.FirstChildElement()->LastChildElement("configuration")->FirstChildElement("fileobject");

	while (fileNode != NULL) {
		Stream stream;
		createStreamFromXml(stream, fileNode);
		if (stream.dstport == _port) {
			insertOrderedIntoList(stream);
		} else {
			assert(stream.srcport == _port);
		}
		fileNode = fileNode->NextSiblingElement("fileobject");
	}
}

std::string Client::streamData() {
	if (streams.empty()) {
		return "";
	}

	Stream nextStream = streams.front();
	streams.pop_front();
	return nextStream.filename;
}

// void Client::streamData() {
// 	while (!streams.empty()) {
// 		Stream nextStream = streams.front();
// 		streams.pop_front();
// 		std::cout << "Duration is: " << std::chrono::duration_cast<std::chrono::microseconds>(nextStream.delay_time).count() << std::endl;
// 		std::this_thread::sleep_for(nextStream.delay_time);
// 
// 		auto now = std::chrono::system_clock::now();
// 		std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
// 		std::cout << "Woke up at time: " << std::ctime(&currentTime) << std::endl;
// 		std::cout << "Read file: " << nextStream.filename << std::endl;
// 
// 	}
// }

Client::Client(uint64_t port, const char* filename) : _port(port) {
	preprocessXml(filename);
	setStartTimestamp();
	assignDuration();
}

// int main(int argc, char* argv[]) {
// 	
// 	// models single threaded version with a single connection
// 	if (argc == 1) {
// 		std::cout << "missing xml filename argument" << "\n";
// 		return -1;
// 	} else if (argc == 2) {
// 		std::cout << "missing port argument" << "\n";
// 		return -1;
// 	}
// 
// 	Client client = Client(atoi(argv[2]), argv[1]);
// 	client.streamData();
// }

}
