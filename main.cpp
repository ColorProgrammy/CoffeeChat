// CoffeeChat v1.01
// Made by ColorProgrammy

// Only for Linux

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <atomic>
#include <vector>
#include <sys/ioctl.h>
#include <net/if.h>

std::atomic<bool> running(true);

std::vector<std::string> get_local_ips() {
    std::vector<std::string> ips;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        return ips;
    }

    struct ifconf ifc;
    char buf[1024];
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
        perror("Error ioctl");
        close(sock);
        return ips;
    }

    struct ifreq* ifr = ifc.ifc_req;
    int interfaces = ifc.ifc_len / sizeof(struct ifreq);
    
    for (int i = 0; i < interfaces; i++) {
        char ip[INET_ADDRSTRLEN];
        if (ifr[i].ifr_addr.sa_family == AF_INET) {
            if (inet_ntop(AF_INET, &((struct sockaddr_in*)&ifr[i].ifr_addr)->sin_addr, ip, sizeof(ip))) {
                if (strcmp(ip, "127.0.0.1") != 0) {
                    ips.push_back(ip);
                }
            }
        }
    }

    close(sock);
    return ips;
}

std::string get_external_ip() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket for external IP");
        return "";
    }

    // Timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct hostent* server = gethostbyname("api.ipify.org");
    if (server == NULL) {
        close(sock);
        return "";
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(80);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return "";
    }

    std::string request = "GET / HTTP/1.1\r\nHost: api.ipify.org\r\nConnection: close\r\n\r\n";
    if (send(sock, request.c_str(), request.length(), 0) < 0) {
        close(sock);
        return "";
    }

    char buffer[1024];
    std::string response;
    int bytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        response += buffer;
    }

    close(sock);

    size_t body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        std::string ip = response.substr(body_start + 4);
        ip.erase(ip.find_last_not_of(" \n\r\t") + 1);
        return ip;
    }

    return "";
}

void receiver(int sock) {
    char buffer[1024];
    while (running) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes <= 0) {
            std::cerr << "\nThe connection was broken" << std::endl;
            running = false;
            break;
        }
        
        buffer[bytes] = '\0';
        std::cout << buffer << "\n> " << std::flush;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage:\n"
                  << "  Server: " << argv[0] << " -s <port>\n"
                  << "  Client: " << argv[0] << " -c <IP:port>\n"
                  << " Use these commands in terminal\n";
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        return 1;
    }
    
    // Server
    if (strcmp(argv[1], "-s") == 0) {
        int port = std::stoi(argv[2]);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        std::cout << "Available IP addresses:\n";
        auto ips = get_local_ips();
        if (ips.empty()) {
            std::cout << "  127.0.0.1 (localhost)\n";
        } else {
            for (const auto& ip : ips) {
                std::cout << "  " << ip << "\n";
            }
        }

        // External IP
        std::cout << "Detecting external IP...\n";
        std::string external_ip = get_external_ip();
        if (!external_ip.empty()) {
            std::cout << "External IP: " << external_ip << std::endl;
        } else {
            std::cout << "External IP: Could not be determined" << std::endl;
        }

        std::cout << "Waiting for connection on port " << port << "...\n";

        int opt = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            perror("Error setsockopt");
            close(sock);
            return 1;
        }

        if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Binding error");
            close(sock);
            return 1;
        }

        listen(sock, 1);

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sock, (sockaddr*)&client_addr, &client_len);
        
        if (client_sock < 0) {
            perror("Error accept");
            close(sock);
            return 1;
        }

        close(sock);
        sock = client_sock;
        std::cout << "Connected from: " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << std::endl;
    }
    // Client
    else if (strcmp(argv[1], "-c") == 0) {
        std::string address(argv[2]);
        size_t colon = address.find(':');
        if (colon == std::string::npos) {
            std::cerr << "Unknown address format. Use: IP:Port" << std::endl;
            close(sock);
            return 1;
        }

        std::string ip = address.substr(0, colon);
        int port = std::stoi(address.substr(colon + 1));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (connect(sock, (sockaddr*)&addr, sizeof(addr))) {
            perror("Connection error");
            close(sock);
            return 1;
        }
        std::cout << "Successfully connected to the server. IP:  " << ip << ":" << port << std::endl;
    }
    else {
        std::cerr << "Unknown mode: " << argv[1] << std::endl;
        close(sock);
        return 1;
    }

    std::thread recv_thread(receiver, sock);
    recv_thread.detach();

    std::string message;
    std::cout << "> ";
    while (running) {
        std::getline(std::cin, message);
        if (!running) break;
        
        if (message == "/exit") {
            running = false;
            break;
        }
        
        if (send(sock, message.c_str(), message.size(), 0) <= 0) {
            perror("Sending error");
            running = false;
            break;
        }
        std::cout << "> ";
    }

    running = false;
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 0;
}
