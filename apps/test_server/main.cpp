#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <thread>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define SOCKET_TYPE SOCKET
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#define SOCKET_TYPE int
#define INVALID_SOCKET_VAL -1
#define CLOSE_SOCKET close
#endif

int main() {

    std::cout << "Started Server" << std::endl;
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
#endif

    SOCKET_TYPE server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET_VAL) {
        std::cout << "Socket Failed" << std::endl;
        perror("socket failed");
        return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in address;
    address.sin_family = AF_INET;
#ifdef _WIN32
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
#else
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif
    address.sin_port = htons(12347);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cout << "Bind Failed" << std::endl;
        perror("bind failed");
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cout << "Listen Failed" << std::endl;
        perror("listen failed");
        return 1;
    }

    std::cout << "TestServer listening on 127.0.0.1:12347..." << std::endl;

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        SOCKET_TYPE client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd == INVALID_SOCKET_VAL) {
            std::cout << "Accept Failed" << std::endl;
            perror("accept failed");
            continue;
        }

        std::cout << "Accepted connection" << std::endl;

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char buf[26];
#ifdef _WIN32
        ctime_s(buf, sizeof(buf), &now);
        std::string time_str = buf;
#else
        std::string time_str = std::ctime(&now);
#endif
        if (!time_str.empty() && time_str.back() == '\n') {
            time_str.pop_back();
        }

        send(client_fd, time_str.c_str(), (int)time_str.length(), 0);
        CLOSE_SOCKET(client_fd);
    }

    CLOSE_SOCKET(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
