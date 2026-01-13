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
    const char* SERVER_IP = "127.0.0.1";

    int sockfd;
    char buffer[BUF_SIZE];

    sockaddr_in server_addr{};

    // 1. Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    cout << "Client socket created.\n";

    // 2. Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    // 3. Connect to server
    if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    cout << "Waiting to connect server on port " << PORT << endl;

    // 4. Receive welcome message
    memset(buffer, 0, BUF_SIZE);
    ssize_t n = recv(sockfd, buffer, BUF_SIZE - 1, 0);
    if (n > 0) {
        cout << buffer << endl;
    }

    cout << "\nEnter # to end the connection\n\n";

    // 5. Chat loop
    while (true) {
        // Send message
        cout << "Client: ";
        cin.getline(buffer, BUF_SIZE);

        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("send");
            break;
        }

        if (buffer[0] == '#')
            break;

        // Receive reply
        memset(buffer, 0, BUF_SIZE);
        n = recv(sockfd, buffer, BUF_SIZE - 1, 0);

        if (n == 0) {
            cout << "Server disconnected.\n";
            break;
        }
        if (n < 0) {
            perror("recv");
            break;
        }

        cout << "Server: " << buffer << endl;

        if (buffer[0] == '#')
            break;
    }

    // 6. Close socket
    close(sockfd);
    cout << "Client terminated.\n";
    return 0;
}