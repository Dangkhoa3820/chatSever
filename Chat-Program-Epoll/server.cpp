#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

using namespace std;

constexpr int PORT = 1500;
constexpr int BUF_SIZE = 1024;
constexpr int MAX_CONNECTION = 100;

atomic<bool> stop{false};
int serverSocket = -1;

mutex mtx;
vector<int> clientSockets;

struct epoll_event ev, events[MAX_CONNECTION + 2];
int epollfd;

void set_non_blocking(int socket)
{
    int flags = fcntl(socket, F_GETFL, 0);

    if (flags == -1)
    {
        perror("fcntl F_GETFL");
        return;
    }

    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl F_SETFL");
        return;
    }
}

void handle_sigint(int)
{
    cout << "\nSIGINT received, shutting down server...\n";
    stop.store(true);
    // shutdown(serverSocket, SHUT_RDWR);
}

void removeClient(int fd)
{
    lock_guard<mutex> lock(mtx);
    clientSockets.erase(
        remove(clientSockets.begin(), clientSockets.end(), fd),
        clientSockets.end());
}

void cleanupClient(int fd)
{
    removeClient(fd);
    close(fd);
}

void handleNewConnection(int serverSocket)
{
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    
    int client_fd = accept(serverSocket, (sockaddr*)&client_addr, &len);
    if (client_fd < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return; // no more pending connections
        if (errno == EINTR)
            return;
        perror("accept");
        return;
    }

    // Add client to poll array
    set_non_blocking(client_fd);
    ev.events = EPOLLIN;
    ev.data.fd = client_fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
        perror("epoll_ctl: client_fd");
        close(client_fd);
        return;
    }
    clientSockets.push_back(client_fd);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    cout << "> Client " << client_fd << " connected from " << ip 
         << ", total: " << clientSockets.size() << ")\n";
}

void handleClientData(int clientFd)
{
    char buffer[BUF_SIZE];
    ssize_t n = recv(clientFd, buffer, BUF_SIZE - 1, 0);
    if (n > 0)
    {
        buffer[n] = '\0';

        // Check for disconnect message
        if (buffer[0] == '#')
        {
            cleanupClient(clientFd);
            cout << "Client " << clientFd << " sent disconnect (total: " << clientSockets.size() << ")\n";
            return;
        }

        cout << "Client " << clientFd << ": " << buffer << "\n";
    }
    else if (n == 0)
    {
        // Connection closed by client
        cleanupClient(clientFd);
        cout << "Client " << clientFd << " closed connection (total: " << clientSockets.size() << ")\n";
    }
    else
    {
        // n < 0: error or would-block
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return; // no more data available right now
        if (errno == EINTR)
            return; // interrupted, try again
        
        // Real error
        perror("recv");
        cleanupClient(clientFd);
        cout << "Client " << clientFd << " error on recv (total: " << clientSockets.size() << ")\n";
    }
}

void handle_send_data()
{
    // Server input → broadcast
    char buffer[BUF_SIZE];
    cin.getline(buffer, BUF_SIZE);
    if(strlen(buffer) == 0)
        return;
    lock_guard<mutex> lock(mtx);
    for (int fd : clientSockets) {
        send(fd, buffer, strlen(buffer), 0);
    }
}

int main()
{
    signal(SIGINT, handle_sigint);

    //Setup server socket
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        perror("socket");
        return 1;
    }

    set_non_blocking(serverSocket);

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(serverSocket, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        return 1;
    }

    if (listen(serverSocket, 128) < 0)
    {
        perror("listen");
        return 1;
    }

    cout << "Server listening on port " << PORT << "...\n";

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        return 1;
    }

    // Add server socket to epoll
    ev.events = EPOLLIN;
    ev.data.fd = serverSocket;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSocket, &ev) == -1) {
        perror("epoll_ctl: serverSocket");
        return 1;
    }

    // Add STDIN to epoll
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) == -1) {
        perror("epoll_ctl: STDIN_FILENO");
        return 1;
    }

    while(!stop.load()) {
        int nready = epoll_wait(epollfd, events, MAX_CONNECTION + 2, 1000);
        if (nready == -1) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nready; i++) {
            if (events[i].data.fd == serverSocket) {
                // New connection
                handleNewConnection(serverSocket);
            } else if (events[i].data.fd == STDIN_FILENO) {
                // Server input
                handle_send_data();
            } else {
                // Client data
                handleClientData(events[i].data.fd);
            }
        }
    }

    // Notify clients about shutdown
    lock_guard<mutex> lock(mtx);
    for (int fd : clientSockets)
    {
        send(fd, "#", 1, 0);
        close(fd);
    }
    close(serverSocket);
    cout << "Server shutdown complete.\n";
}
