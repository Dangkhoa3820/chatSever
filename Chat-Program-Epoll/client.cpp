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
#include <termios.h>
#include <vector>
#include <deque>

using namespace std;

constexpr int PORT = 1500;
constexpr int BUF_SIZE = 1024;
constexpr int MAX_MESSAGES = 100;

atomic<bool> stop{false};
int clientSocket = -1;

epoll_event ev, events[2];
int epollfd;

mutex displayMutex;
deque<string> messageHistory;
string currentInput;

termios originalTermios;

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

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &originalTermios);
    termios raw = originalTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);
}

void clearScreen()
{
    cout << "\033[2J\033[H" << flush;
}

void redrawScreen()
{
    lock_guard<mutex> lock(displayMutex);
    
    // Clear screen and move cursor to top
    cout << "\033[2J\033[H";
    
    // Display message history from top
    for (const auto& msg : messageHistory)
    {
        cout << msg << "\n";
    }
    
    // Move to bottom of screen (save cursor position)
    cout << "\033[999;1H";
    
    // Draw input line at bottom
    cout << "\033[K> " << currentInput << flush;
}

void addMessage(const string& msg)
{
    lock_guard<mutex> lock(displayMutex);
    messageHistory.push_back(msg);
    if (messageHistory.size() > MAX_MESSAGES)
        messageHistory.pop_front();
}

void handle_sigint(int)
{
    cout << "\nSIGINT received, shutting down client...\n";
    stop.store(true); // signal-safe
    disableRawMode();
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

    string username;
    cout << "Enter your username: ";
    getline(cin, username);
    string join = "JOIN " + username + "\n";
    send(clientSocket, join.c_str(), join.size(), 0);

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

    // Enable raw mode and setup display
    enableRawMode();
    clearScreen();
    redrawScreen();

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
                    addMessage("Server closed connection.");
                    redrawScreen();
                    stop.store(true);
                    break;
                }

                addMessage(string(buffer));
                redrawScreen();
            }
            else if (events[i].data.fd == STDIN_FILENO)
            {
                char c;
                if (read(STDIN_FILENO, &c, 1) == 1)
                {
                    if (c == '\n')
                    {
                        // Enter pressed - send message
                        if (!currentInput.empty())
                        {
                            string msg = currentInput + "\n";
                            if (send(clientSocket, msg.c_str(), msg.length(), 0) <= 0)
                            {
                                addMessage("Failed to send data to server.");
                                redrawScreen();
                                stop.store(true);
                                break;
                            }
                            currentInput.clear();
                            redrawScreen();
                        }
                    }
                    else if (c == 127 || c == 8)
                    {
                        // Backspace
                        if (!currentInput.empty())
                        {
                            currentInput.pop_back();
                            redrawScreen();
                        }
                    }
                    else if (c >= 32 && c <= 126)
                    {
                        // Printable character
                        currentInput += c;
                        redrawScreen();
                    }
                }
            }
        }
    }

    // Cleanup
    clearScreen();
    disableRawMode();
    
    // Notify server about shutdown
    send(clientSocket, "#", 1, 0);

    shutdown(clientSocket, SHUT_RDWR); // wake recv/send
    close(clientSocket);               // release fd

    cout << "You left the chat.\n";
}