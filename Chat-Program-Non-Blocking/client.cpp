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

using namespace std;

constexpr int PORT = 1500;
constexpr int BUF_SIZE = 1024;

atomic<bool> stop{false};
int clientSocket = -1;

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


void handle_sigint(int) {
    stop.store(true);   // signal-safe
}

int main() {
    signal(SIGINT, handle_sigint);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* serverAddress = nullptr;
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
    // Receive thread
    thread recvThread([&]() {
        char buffer[BUF_SIZE];

        while (!stop.load()) {
            ssize_t n = recv(clientSocket, buffer, BUF_SIZE - 1, 0);
            if (n <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    this_thread::sleep_for(chrono::milliseconds(50));
                    continue;
                }
                stop.store(true);
                break; // real error
            }

            buffer[n] = '\0';

            if (buffer[0] == '#') {
                cout << "Server closed connection.\n";
                stop.store(true);
                break;
            }

            cout << "Server: " << buffer << endl;
        }
    });

    // Send thread
    thread sendThread([&]() {
        char buffer[BUF_SIZE];

        while (!stop.load()) {
            if (!cin.getline(buffer, BUF_SIZE))
                break;

            if (strlen(buffer) == 0)
                continue;

            if (send(clientSocket, buffer, strlen(buffer), 0) <= 0) {
                stop.store(true);
                break;
            }
        }
    });

    // ---- Controlled shutdown (single place) ----
    while (!stop.load())
        this_thread::sleep_for(chrono::milliseconds(100));

    //Notify server about shutdown
    send(clientSocket, "#", 1, 0);

    shutdown(clientSocket, SHUT_RDWR); // wake recv/send
    close(clientSocket);               // release fd

    recvThread.join();
    sendThread.join();

    cout << "Client leaved the chat.\n";
}