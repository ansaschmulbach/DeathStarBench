#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <input_file>\n";
        return 1;
    }

    int port = std::atoi(argv[1]);
    const char* input_filename = argv[2];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in serv_addr {};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    std::ifstream infile(input_filename, std::ios::binary);
    if (!infile) {
        std::cerr << "Could not open input file.\n";
        close(sock);
        return 1;
    }

    // Send the file
    char buffer[BUFFER_SIZE];
    while (infile.read(buffer, BUFFER_SIZE) || infile.gcount() > 0) {
        std::streamsize bytesRead = infile.gcount();
        ssize_t totalSent = 0;
        while (totalSent < bytesRead) {
            ssize_t sent = send(sock, buffer + totalSent, bytesRead - totalSent, 0);
            if (sent <= 0) {
                perror("Send failed");
                infile.close();
                close(sock);
                return 1;
            }
            totalSent += sent;
        }
    }

    infile.close();

    std::cout << "File sent successfully. Now waiting for server response...\n";

    // Receive data from the server until the server closes connection
    while (true) {
        ssize_t received = recv(sock, buffer, BUFFER_SIZE, 0);
        if (received < 0) {
            perror("recv failed");
            close(sock);
            return 1;
        } else if (received == 0) {
            // Server closed connection
            std::cout << "Server closed the connection.\n";
            break;
        } else {
            // Print received data (assuming text; if binary, handle accordingly)
            std::cout.write(buffer, received);
            std::cout.flush();
        }
    }

    close(sock);
    return 0;
}

