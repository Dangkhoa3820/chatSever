#include <iostream>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <csignal>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

using namespace std;

constexpr int PORT = 1500;
constexpr int BUF_SIZE = 1024;

atomic<bool> stop{false};
int clientSocket = -1;

epoll_event ev, events[2];
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
    cout << "\nSIGINT received, shutting down client...\n";
    stop.store(true); // signal-safe
}

int main()
{
    signal(SIGINT, handle_sigint);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *serverAddress = nullptr;
    string serverIp;

    cout << "Server IPv4 address> ";
    getline(cin, serverIp);

    if (getaddrinfo(serverIp.c_str(), to_string(PORT).c_str(),
                    &hints, &serverAddress) != 0)
        throw runtime_error("getaddrinfo failed");

    clientSocket = socket(
        serverAddress->ai_family,
        serverAddress->ai_socktype,
        serverAddress->ai_protocol);

    if (clientSocket < 0)
        throw runtime_error("socket failed");

    if (connect(clientSocket,
                serverAddress->ai_addr,
                serverAddress->ai_addrlen) != 0)
        throw runtime_error("connect failed");

    freeaddrinfo(serverAddress);

    set_non_blocking(clientSocket);

    cout << "Connected to server " << serverIp << ":" << PORT << "\n";

    epollfd = epoll_create1(0);
    if (epollfd == -1)
    {
        perror("epoll_create1");
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = clientSocket;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientSocket, &ev) == -1)
    {
        perror("epoll_ctl: clientSocket");
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) == -1)
    {
        perror("epoll_ctl: STDIN_FILENO");
        return 1;
    }

    while (!stop.load())
    {
        int nready = epoll_wait(epollfd, events, 2, 200);
        if (nready < 0)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nready; ++i)
        {
            if (events[i].data.fd == clientSocket)
            {
                char buffer[BUF_SIZE];
                ssize_t n = recv(clientSocket, buffer, BUF_SIZE - 1, 0);
                if (n <= 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        this_thread::sleep_for(chrono::milliseconds(50));
                        continue;
                    }
                    stop.store(true);
                    break; // real error
                }

                buffer[n] = '\0';

                if (buffer[0] == '#')
                {
                    cout << "Server closed connection.\n";
                    stop.store(true);
                    break;
                }

                cout << "Server: " << buffer << endl;
            }
            else if (events[i].data.fd == STDIN_FILENO)
            {
                char buffer[BUF_SIZE];
                cin.getline(buffer, BUF_SIZE);

                if (strlen(buffer) == 0)
                    continue;

                if (send(clientSocket, buffer, strlen(buffer), 0) <= 0)
                {
                    cout << "Failed to send data to server.\n";
                    stop.store(true);
                    break;
                }
            }
        }
    }

    // Notify server about shutdown
    send(clientSocket, "#", 1, 0);

    shutdown(clientSocket, SHUT_RDWR); // wake recv/send
    close(clientSocket);               // release fd

    cout << "Client left the chat.\n";
}