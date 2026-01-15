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
#include <poll.h>

#define MAX_CONNECTION 100

using namespace std;

constexpr int PORT = 1500;
constexpr int BUF_SIZE = 1024;

atomic<bool> stop{false};
int serverSocket = -1;

mutex mtx;
vector<int> clientSockets;

struct pollfd clientFds[MAX_CONNECTION + 1];
int nfds = 0;

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

void cleanupClient(int slot)
{
    int fd = clientFds[slot].fd;

    removeClient(fd);
    close(fd);

    clientFds[slot].fd = -1;
    clientFds[slot].revents = 0;
    --nfds;
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

    // Find empty slot for new client
    int slot = -1;
    for (int i = 1; i <= MAX_CONNECTION; i++)
    {
        if (clientFds[i].fd == -1)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        cout << "Max clients reached, rejecting connection\n";
        close(client_fd);
        return;
    }

    // Add client to poll array
    set_non_blocking(client_fd);
    clientFds[slot].fd = client_fd;
    clientFds[slot].events = POLLIN | POLLRDHUP;
    nfds++;

    lock_guard<mutex> lock(mtx);
    clientSockets.push_back(client_fd);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    cout << "> Client " << client_fd << " connected from " << ip 
         << " (slot " << slot << ", total: " << nfds << ")\n";
}

void handleClientData(int slot)
{
    char buffer[BUF_SIZE];
    int clientFd = clientFds[slot].fd;
    
    ssize_t n = recv(clientFd, buffer, BUF_SIZE - 1, 0);
    if (n > 0)
    {
        buffer[n] = '\0';

        // Check for disconnect message
        if (buffer[0] == '#')
        {
            cleanupClient(slot);
            cout << "Client " << clientFd << " sent disconnect (slot " << slot << ") (total: " << nfds << ")\n";
            return;
        }

        cout << "Client " << clientFd << ": " << buffer << "\n";
    }
    else if (n == 0)
    {
        // Connection closed by client
        cleanupClient(slot);
        cout << "Client " << clientFd << " closed connection (slot " << slot << ") (total: " << nfds << ")\n";
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
        cleanupClient(slot);
        cout << "Client " << clientFd << " error on recv (slot " << slot << ") (total: " << nfds << ")\n";
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

    for (int i = 1; i < MAX_CONNECTION + 1; i++)
    {
        clientFds[i].fd = -1; // reset
    }

    // Accept thread - handles incoming connections and client data
    thread acceptThread([&]()
    {
        while (!stop.load())
        {
            clientFds[0].fd = serverSocket;
            clientFds[0].events = POLLIN;

            int ready = poll(clientFds, 1 + MAX_CONNECTION, 1000);
            if (ready < 0)
            {
                if (errno == EINTR)
                    continue;
                perror("poll");
                break;
            }
            
            // Check for new connections
            if (clientFds[0].revents & POLLIN)
            {
                handleNewConnection(serverSocket);
            }
            
            // Check all client sockets for incoming data
            for (int i = 1; i <= MAX_CONNECTION; i++)
            {
                if (clientFds[i].fd != -1 && (clientFds[i].revents & POLLIN))
                {
                    handleClientData(i);
                }
            }
        }
    });

    // Server input → broadcast
    thread sendThread([&]()
                      {
        char buffer[BUF_SIZE];

        while (!stop.load()) {
            struct pollfd fds;
            fds.fd = STDIN_FILENO;
            fds.events = POLLIN;
            int ready = poll(&fds, 1, 200); // 200ms timeout
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                perror("poll");
                break;
            }
            
            if(fds.revents & POLLIN) {
                cin.getline(buffer, BUF_SIZE);
                if(strlen(buffer) == 0)
                    continue;
                lock_guard<mutex> lock(mtx);
                for (int fd : clientSockets) {
                    send(fd, buffer, strlen(buffer), 0);
                }
            }
        } });

    acceptThread.join();
    sendThread.join();

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
