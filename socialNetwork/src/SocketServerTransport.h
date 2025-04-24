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

#include <sys/un.h> // Header for Unix domain sockets

enum SocketType {
    UNIX_DOMAIN,
    TCP_SOCKET
};

class SocketServerTransport : public TServerTransport {
private:
    int socketFd;         // Unix domain socket file descriptor
    std::string socketPath; // Path to the Unix socket file
	int port;              // Port number (used for TCP sockets only)
    SocketType socketType; // Type of the socket
    bool isListening;

public:
    explicit SocketServerTransport(SocketType type, const std::string& path = "", int portNum = 0)
        : socketFd(THRIFT_INVALID_SOCKET), socketPath(path), port(portNum), socketType(type), isListening(false) {}

protected:
    std::shared_ptr<TTransport> acceptImpl() override {
        if (socketFd == THRIFT_INVALID_SOCKET) {
            throw TTransportException(TTransportException::NOT_OPEN, "Socket not initialized");
        }

	std::cout << "socket fd: " << socketFd << std::endl;
        int clientSocket;
        if (socketType == UNIX_DOMAIN) {
            struct sockaddr_un clientAddress;
            socklen_t size = sizeof(clientAddress);
            clientSocket = ::accept(socketFd, (struct sockaddr*)&clientAddress, &size);
        } else { // TCP_SOCKET
            struct sockaddr_in clientAddress;
            socklen_t size = sizeof(clientAddress);
            clientSocket = ::accept(socketFd, (struct sockaddr*)&clientAddress, &size);
        }

        // struct sockaddr_un clientAddress;
        // socklen_t size = sizeof(clientAddress);
        // int clientSocket = ::accept(socketFd, (struct sockaddr*)&clientAddress, &size);

        if (clientSocket == THRIFT_INVALID_SOCKET) {
            int errno_copy = errno;
            throw TTransportException(TTransportException::UNKNOWN, "Error accepting connection", errno_copy);
        }

        auto transportPair = std::make_shared<TTransportPair>(
            std::make_shared<TFDTransport>(clientSocket), // Reading from socket
            std::make_shared<TFDTransport>(clientSocket)  // Writing to socket
        );
        return transportPair;
    }

public:
    void createServerSocket() {
	
        if (socketType == UNIX_DOMAIN) {
            // Create Unix domain socket
            socketFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (socketFd == THRIFT_INVALID_SOCKET) {
                throw TTransportException(TTransportException::UNKNOWN, "Unable to create Unix socket");
            }

            struct sockaddr_un serverAddr{};
            serverAddr.sun_family = AF_UNIX;
            std::strncpy(serverAddr.sun_path, socketPath.c_str(), sizeof(serverAddr.sun_path) - 1);

            ::unlink(socketPath.c_str());
            if (::bind(socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                int errno_copy = errno;
                ::close(socketFd);
                throw TTransportException(TTransportException::UNKNOWN, "Unable to bind Unix socket", errno_copy);
            }
        } else if (socketType == TCP_SOCKET) {
            // Create TCP socket
            socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (socketFd == THRIFT_INVALID_SOCKET) {
                throw TTransportException(TTransportException::UNKNOWN, "Unable to create TCP socket");
            }

            struct sockaddr_in serverAddr{};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            serverAddr.sin_port = htons(port);

            if (::bind(socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                int errno_copy = errno;
                ::close(socketFd);
                throw TTransportException(TTransportException::UNKNOWN, "Unable to bind TCP socket", errno_copy);
            }
        } else {
            throw TTransportException(TTransportException::BAD_ARGS, "Invalid socket type");
        }

        // socketFd = ::socket(AF_UNIX, SOCK_STREAM, 0); // Unix domain socket
        // if (socketFd == THRIFT_INVALID_SOCKET) {
        //     throw TTransportException(TTransportException::UNKNOWN, "Unable to create Unix socket");
        // }

        // struct sockaddr_un serverAddr{};
        // serverAddr.sun_family = AF_UNIX;
        // std::strncpy(serverAddr.sun_path, socketPath.c_str(), sizeof(serverAddr.sun_path) - 1);

        // // Remove any existing socket file
        // ::unlink(socketPath.c_str());

        // if (::bind(socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        //     int errno_copy = errno;
        //     ::close(socketFd);
        //     throw TTransportException(TTransportException::UNKNOWN, "Unable to bind Unix socket", errno_copy);
        // }

        if (::listen(socketFd, SOMAXCONN) < 0) {
            int errno_copy = errno;
            ::close(socketFd);
            throw TTransportException(TTransportException::UNKNOWN, "Unable to listen on Unix socket", errno_copy);
        }

        isListening = true;
        std::cout << "Listening on Unix socket: " << socketPath << "..." << std::endl;
    }

    void open() {
        if (socketFd != THRIFT_INVALID_SOCKET) return;
        createServerSocket();
    }

    bool isOpen() const {
        return socketFd != THRIFT_INVALID_SOCKET;
    }

    void listen() override {
        if (!isOpen()) {
		open();
        }
        isListening = true;
        std::cout << "Listening for Unix socket connections..." << std::endl;
    }

    void interrupt() override {
        isListening = false;
        std::cout << "Server interrupted, stopping listener..." << std::endl;
    }

    void interruptChildren() override {
        // Optionally, handle child connections
    }

    void close() override {
        if (isOpen()) {
            ::close(socketFd);
            socketFd = THRIFT_INVALID_SOCKET;
            isListening = false;
			if (socketType == UNIX_DOMAIN) {
                ::unlink(socketPath.c_str());
            }
            // ::unlink(socketPath.c_str()); // Remove the socket file
            std::cout << "Unix socket transport closed and file unlinked." << std::endl;
        }
    }

    ~SocketServerTransport() {
        close(); // Ensure cleanup
    }
};


}
