#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

int main() {
    const int PORT = 1500;
    const int BUF_SIZE = 1024;

    int listen_fd, conn_fd;
    char buffer[BUF_SIZE];

    sockaddr_in server_addr{};
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    // 1. Create socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    // 2. Allow quick reuse of port
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // 4. Bind
    if (bind(listen_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    // 5. Listen
    if (listen(listen_fd, 1) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    cout << "Server listening on port " << PORT << "...\n";

    // 6. Accept client
    conn_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
    if (conn_fd < 0) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    cout << "Client connected from "
         << inet_ntoa(client_addr.sin_addr) << endl;

    // Send welcome message
    const char* welcome_msg = "Connected to chat server!\n";
    send(conn_fd, welcome_msg, strlen(welcome_msg), 0);

    // 7. Chat loop
    while (true) {
        // Receive from client
        memset(buffer, 0, BUF_SIZE);
        ssize_t n = recv(conn_fd, buffer, BUF_SIZE - 1, 0);

        if (n == 0) {
            cout << "Client disconnected.\n";
            break;
        }
        if (n < 0) {
            perror("recv");
            break;
        }

        cout << "Client: " << buffer << endl;

        if (buffer[0] == '#')
            break;

        // Send response
        cout << "Server: ";
        cin.getline(buffer, BUF_SIZE);

        if (send(conn_fd, buffer, strlen(buffer), 0) < 0) {
            perror("send");
            break;
        }

        if (buffer[0] == '#')
            break;
    }

    // 8. Cleanup
    close(conn_fd);
    close(listen_fd);

    cout << "Server terminated.\n";
    return 0;
}