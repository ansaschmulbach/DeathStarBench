#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
#include <iomanip>

struct StreamData {
    int src_pid;
    int dst_pid;
    std::vector<uint8_t> data;
};

void parsePIDs(const std::string& line, int& src_pid, int& dst_pid) {
    std::size_t src_start = line.find("STREAM PID") + 11;
    std::size_t src_end = line.find('.', src_start);
    std::size_t dst_start = line.find('>', src_end) + 2;
    std::size_t dst_end = line.find('.', dst_start);
    
    src_pid = std::stoi(line.substr(src_start, src_end - src_start));
    dst_pid = std::stoi(line.substr(dst_start, dst_end - dst_start));
}

std::vector<StreamData> processFile(const std::string& fileName) {
    std::vector<StreamData> streams;
    std::ifstream file(fileName);
    std::string line;
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << fileName << std::endl;
        return streams;
    }
    
    while (std::getline(file, line)) {
        if (line.find("STREAM PID") != std::string::npos) {
            StreamData streamData;
	    parsePIDs(line, streamData.src_pid, streamData.dst_pid);

            // std::istringstream pidStream(line);
            // std::string token;

            // pidStream >> token >> token >> streamData.src_pid;
            // pidStream.ignore(1, '.');
            // pidStream >> token >> streamData.dst_pid;

            std::getline(file, line); // Skip the command line
            std::getline(file, line); // Skip the command line
            std::getline(file, line); // Skip the command line

            while (std::getline(file, line) && !line.empty() && line[0] != '=') {
		std::istringstream dataStream(line.substr(10, 48));
                std::string byteStr;
		for (int i = 0; i < 16; i++) {
                    dataStream >> byteStr;
                    if (byteStr.size() == 2) {
                        streamData.data.push_back(static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16)));
                    }
                }
            }

	    if (streamData.dst_pid == 40016) {
            	streams.push_back(streamData);
	    }
        }
    }
    
    file.close();
    return streams;
}

void writeBinaryFiles(const std::vector<StreamData>& streams, std::string fileName) {

    std::ofstream outFile(fileName, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open file for writing: " << fileName << std::endl;
        return;
    }
    
    for (const auto& stream : streams) {
        outFile.write(reinterpret_cast<const char*>(stream.data.data()), stream.data.size());
    }
    
    outFile.close();

    // int id = 0;
    // for (const auto& stream : streams) {
    //     std::string fileName = "stream_" + std::to_string(stream.src_pid) + "-" + std::to_string(id) + ".bin";
    //     std::ofstream outFile(fileName, std::ios::binary);
    //     if (!outFile.is_open()) {
    //         std::cerr << "Failed to open file for writing: " << fileName << std::endl;
    //         continue;
    //     }
    //     
    //     outFile.write(reinterpret_cast<const char*>(stream.data.data()), stream.data.size());
    //     outFile.close();
    //     id++;
    // }
}

int main() {
    std::string fileName = "user_mention_out";
    std::vector<StreamData> streams = processFile(fileName);
    
    writeBinaryFiles(streams, "stream-39320.bin");
    
    return 0;
}
