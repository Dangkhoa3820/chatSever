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

using namespace std;

constexpr int PORT = 1500;
constexpr int BUF_SIZE = 1024;

atomic<bool> stop{false};
int serverSocket = -1;

mutex mtx;
vector<int> clientSockets;
vector<thread> clientThreads;

struct pollfd fds[2];

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
    //shutdown(serverSocket, SHUT_RDWR);
}

void removeClient(int fd)
{
    lock_guard<mutex> lock(mtx);
    clientSockets.erase(
        remove(clientSockets.begin(), clientSockets.end(), fd),
        clientSockets.end());
}

void clientReceiveLoop(int fd)
{
    char buffer[BUF_SIZE];
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!stop.load())
    {
        int ready = poll(&pfd, 1, 200); // 200ms timeout
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        if (ready == 0) // timeout
            continue;

        if (pfd.revents & POLLIN) {
            ssize_t n = recv(fd, buffer, BUF_SIZE - 1, 0);
            if (n <= 0) {
                removeClient(fd);
                close(fd);
                break; // connection closed or error
            }

            buffer[n] = '\0';

            if (buffer[0] == '#') {
                cout << "Client " << fd << " disconnected.\n";
                removeClient(fd);
                close(fd);
                break;
            }

            lock_guard<mutex> lock(mtx);
            cout << "Client " << fd << ": " << buffer << endl;
        }

        if (pfd.revents & (POLLERR | POLLHUP)) {
            break; // connection error
        }
    }
}

int main()
{
    signal(SIGINT, handle_sigint);

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

    if (listen(serverSocket, 8) < 0)
    {
        perror("listen");
        return 1;
    }

    cout << "Server listening on port " << PORT << "...\n";

    // Accept thread
    thread acceptThread([&]()
                        {
        while (!stop.load()) {
            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);

            fds[0].fd = serverSocket;
            fds[0].events = POLLIN;

            int ready = poll(&fds[0], 1, 200); // 200ms timeout
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                perror("poll");
                break;
            }

            if(fds[0].revents & POLLIN) {

                int fd = accept(serverSocket, (sockaddr*)&client_addr, &len);
                if (fd < 0) {
                    this_thread::sleep_for(chrono::milliseconds(100));
                    continue;
                }

                set_non_blocking(fd);

                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

                {
                    lock_guard<mutex> lock(mtx);
                    clientSockets.push_back(fd);
                    clientThreads.emplace_back(clientReceiveLoop, fd);
                }
                cout << "> Client " << fd << " connected from " << ip << "\n";
            }
        } });

    // Server input → broadcast
    thread sendThread([&]()
                      {
        char buffer[BUF_SIZE];

        while (!stop.load()) {
            fds[1].fd = STDIN_FILENO;
            fds[1].events = POLLIN;
            int ready = poll(&fds[1], 1, 200); // 200ms timeout
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                perror("poll");
                break;
            }
            
            if(fds[1].revents & POLLIN) {
                cin.getline(buffer, BUF_SIZE);
                if(strlen(buffer) == 0)
                    continue;
                lock_guard<mutex> lock(mtx);
                for (int fd : clientSockets) {
                    send(fd, buffer, strlen(buffer), 0);
                }
            }
        }
    });

    acceptThread.join();

    for (auto &t : clientThreads)
        if (t.joinable())
            t.join();

    sendThread.join();

    // Notify clients about shutdown
    lock_guard<mutex> lock(mtx);
    for (int fd : clientSockets) {
        send(fd, "#", 1, 0);
        close(fd);
    }
    close(serverSocket);
    cout << "Server shutdown complete.\n";
}
