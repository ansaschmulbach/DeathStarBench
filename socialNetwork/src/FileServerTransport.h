#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <cstddef>
#include <string>
#include <thrift/transport/TFDTransport.h>
#include <thrift/transport/PlatformSocket.h>
#include <thrift/transport/TServerTransport.h>
#include <thrift/transport/TFileTransport.h>
#include <thrift/transport/TTransportException.h>
#include "ServerTransportUtils.h"

namespace social_network{

using apache::thrift::transport::TFramedTransport;
using apache::thrift::transport::TFDTransport;
using apache::thrift::transport::TServerTransport;
using apache::thrift::transport::TFileTransport;
using apache::thrift::transport::TTransportException;

class FileServerTransport : public TServerTransport {
private:
    std::string filePath;
    int inFd;
    int outFd;
    bool isListening;

public:
    explicit FileServerTransport(const std::string& filePath) : filePath(filePath) { 
        inFd = 0; 
        outFd = 0; 
		isListening = false;
    }


protected:
    std::shared_ptr<TTransport> acceptImpl() override {
	 if (isEOF(this->inFd)) {	
            exit(0);
            // throw TTransportException(TTransportException::UNKNOWN, "reached EOF");
	 }
	// std::cout << "acceptImpl: this->outFd: " << this->outFd << std::endl;
		std::shared_ptr<TFDTransport> inTransport(new TFDTransport(this->inFd));
		// std::shared_ptr<TFramedTransport> inTransport(new TFramedTransport(inFile));
		std::shared_ptr<TFDTransport> outTransport(new TFDTransport(this->outFd));
		// std::shared_ptr<TFramedTransport> outTransport(new TFramedTransport(outFile));
		auto transportPair = std::make_shared<TTransportPair>(
				inTransport,
				outTransport
		);
		return transportPair;
    }

public:

    void open() {
		if (isOpen()) return;
        this->inFd = ::open(filePath.c_str(), O_RDONLY);
	    if (-1 == inFd) {
            throw TTransportException(TTransportException::UNKNOWN, "unable to open server in file descriptor");
	    }

        this->outFd = ::open("out", O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IXUSR);
	    if (-1 == outFd) {
            throw TTransportException(TTransportException::UNKNOWN, "unable to open server out file descriptor");
	    }
	// std::cout << "outFd: " << outFd << std::endl;
    }

    bool isOpen() const {
	    return this->inFd != 0;
    }

    void listen() override {
        open(); // Ensure the file is open
        isListening = true; // Set the server to listening state
        std::cout << "Listening for file transport connections..." << std::endl;
    }

    void interrupt() override {
        isListening = false; // Disable listening
        std::cout << "Server interrupted, stopping listener..." << std::endl;
    }

    void interruptChildren() override {
    }

    void close() override {
        isListening = false; // Ensure the listening state is reset
		this->inFd = 0;
		this->outFd = 0;
        if (this->isOpen()) {
			::close(this->outFd);
			::close(this->inFd);
            std::cout << "File transport closed." << std::endl;
        }
    }

    ~FileServerTransport() {
        close(); // Cleanup on object destruction
    }

};

}
