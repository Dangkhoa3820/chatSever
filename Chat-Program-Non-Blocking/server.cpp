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

using namespace std;

constexpr int PORT = 1500;
constexpr int BUF_SIZE = 1024;

atomic<bool> stop{false};
int serverSocket = -1;

mutex mtx;
vector<int> clientSockets;
vector<thread> clientThreads;

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

    while (!stop.load())
    {
        ssize_t n = recv(fd, buffer, BUF_SIZE - 1, 0);
        if (n <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                this_thread::sleep_for(chrono::milliseconds(50));
                continue;
            }
            break; // real error
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

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(serverSocket, &readfds);
            int maxfd = serverSocket;
            timeval timeout{};
            timeout.tv_sec = 0.2;

            int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &timeout);
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                perror("select");
                break;
            }

            if(FD_ISSET(serverSocket, &readfds)) {

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
            if (!cin.getline(buffer, BUF_SIZE))
                break;

            lock_guard<mutex> lock(mtx);
            for (int fd : clientSockets)
                send(fd, buffer, strlen(buffer), 0);
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
