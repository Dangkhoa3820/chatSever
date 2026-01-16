# Simple Chat Server and Client Program

This project implements a TCP-based chat server and client application using four different I/O models in C++. Each implementation demonstrates different approaches to handling multiple concurrent client connections.

## Project Structure

```
Simple-Server-and-Chat-Program/
├── Chat-Program-Multithread/      # Thread-per-client blocking I/O
├── Chat-Program-Non-Blocking/     # Non-blocking I/O with threads
├── Chat-Program-Polling/          # poll() system call I/O multiplexing
├── Chat-Program-Epoll/            # epoll() system call I/O multiplexing
└── README.md
```

---

## 1. Multithread Implementation

**Directory:** `Chat-Program-Multithread/`

### Overview
The multithread approach spawns a dedicated thread for each connected client. Each thread blocks on `recv()` waiting for data from its assigned client.

### Technical Details

#### Key Technologies:
- **Blocking I/O**: Uses standard blocking socket operations
- **`std::thread`**: Creates separate threads for each client connection
- **`std::mutex`**: Protects shared resources (client list, console output)
- **`std::atomic<bool>`**: Thread-safe flag for graceful shutdown

#### Architecture:
```
Main Thread:
  └─> accept() loop (blocking)
       └─> spawns new thread per client

Client Thread (N threads):
  └─> recv() loop (blocking)
       └─> process message
       └─> broadcast to other clients
```

#### Advantages:
- Simple and straightforward implementation
- Natural isolation between clients
- Easy to understand and debug

#### Disadvantages:
- High memory overhead (each thread consumes stack space ~1MB)
- Context switching overhead with many threads
- Not scalable beyond ~1000 concurrent clients
- Thread creation/destruction overhead

#### Use Case:
Best for applications with moderate numbers of clients (< 100) where simplicity is more important than scalability.

---

## 2. Non-Blocking I/O Implementation

**Directory:** `Chat-Program-Non-Blocking/`

### Overview
Uses non-blocking sockets with threads, where each thread handles one client but doesn't block on I/O operations. Instead, it polls the socket status.

### Technical Details

#### Key Technologies:
- **Non-blocking I/O**: `fcntl()` with `O_NONBLOCK` flag
- **`std::thread`**: One thread per client (like multithread)
- **Error handling**: `EAGAIN`/`EWOULDBLOCK` for retry logic
- **`std::this_thread::sleep_for()`**: Prevents busy-waiting

#### Architecture:
```
Main Thread:
  └─> accept() loop (non-blocking)
       └─> spawns thread per client

Client Thread (N threads):
  └─> recv() with EAGAIN handling
       └─> if EAGAIN: sleep and retry
       └─> if data: process and broadcast
```

#### Key Functions:
```cpp
void set_non_blocking(int socket) {
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}
```

#### Advantages:
- Threads don't block indefinitely
- Can implement timeouts easily
- Better control over I/O operations

#### Disadvantages:
- Still creates one thread per client
- Busy-waiting (even with sleep) wastes CPU cycles
- Not significantly more scalable than blocking approach
- More complex error handling

#### Use Case:
Useful when you need timeout capabilities or want to perform other tasks while waiting for I/O, but still acceptable to have multiple threads.

---

## 3. Poll Implementation

**Directory:** `Chat-Program-Polling/`

### Overview
Uses the `poll()` system call for I/O multiplexing. A single thread monitors multiple file descriptors simultaneously, allowing one thread to handle many clients efficiently.

### Technical Details

#### Key Technologies:
- **`poll()` system call**: POSIX I/O multiplexing interface
- **Non-blocking I/O**: Combined with poll for efficiency
- **`struct pollfd`**: Array of file descriptors to monitor
- **Event-driven**: Reacts only when data is available

#### Architecture:
```
Single Main Thread:
  └─> poll() waits on all file descriptors
       ├─> Server socket ready → accept new client
       ├─> STDIN ready → broadcast server message
       └─> Client socket ready → handle client data
```

#### Key Data Structures:
```cpp
struct pollfd clientFds[MAX_CONNECTION + 1];
clientFds[i].fd = socket;
clientFds[i].events = POLLIN;  // Monitor for input
```

#### Advantages:
- Single-threaded event loop (no threading overhead)
- Scalable to hundreds of clients
- Lower memory footprint
- Portable across Unix-like systems

#### Disadvantages:
- Linear scan of file descriptors (O(n) complexity)
- Less efficient than epoll for thousands of connections
- Array-based implementation requires MAX_CONNECTION limit

#### Use Case:
Good for applications with dozens to hundreds of concurrent clients where portability is important (works on macOS, Linux, BSD).

---

## 4. Epoll Implementation

**Directory:** `Chat-Program-Epoll/`

### Overview
Uses the `epoll()` system call, which is Linux's most efficient I/O multiplexing mechanism. Provides O(1) performance for monitoring large numbers of file descriptors.

### Technical Details

#### Key Technologies:
- **`epoll_create1()`**: Creates epoll instance
- **`epoll_ctl()`**: Registers/modifies/removes file descriptors
- **`epoll_wait()`**: Waits for events on registered descriptors
- **Edge-triggered/Level-triggered**: Can use either mode
- **Non-blocking I/O**: Essential for proper epoll usage

#### Architecture:
```
Single Main Thread:
  └─> epoll_wait() blocks until events
       ├─> EPOLLIN on server socket → handleNewConnection()
       ├─> EPOLLIN on STDIN → handle_send_data()
       └─> EPOLLIN on client socket → handleClientData()
            └─> read loop until EAGAIN
            └─> parse line-delimited messages
            └─> broadcast to other clients
```

#### Key Functions:
```cpp
// Create epoll instance
int epollfd = epoll_create1(0);

// Add file descriptor
epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);

// Wait for events
epoll_wait(epollfd, events, MAX_EVENTS, timeout);
```

#### Advanced Features:
- **Message buffering**: Accumulates partial messages across multiple reads
- **Line-delimited protocol**: Messages separated by `\n`
- **JOIN handshake**: `JOIN username\n` for client identification
- **Disconnect protocol**: `#` for graceful disconnection

#### Client Features (Enhanced):
- **Raw terminal mode**: Character-by-character input
- **Bottom input line**: Input always at bottom, messages scroll above
- **ANSI escape codes**: Terminal control for cursor positioning
- **Real-time display**: Screen updates on every character typed

#### Advantages:
- **O(1) performance**: Only notified of ready file descriptors
- **Highly scalable**: Can handle 10,000+ concurrent connections
- **Most efficient**: Lower CPU usage than poll
- **Linux-optimized**: Takes advantage of kernel's red-black tree

#### Disadvantages:
- **Linux-only**: Not portable to macOS or Windows
- **More complex**: Requires understanding of edge-triggered semantics
- **Proper loop reading**: Must read until EAGAIN in each event

#### Use Case:
Best for high-performance Linux servers handling thousands of concurrent connections (web servers, chat servers, game servers).

---

## Common Features Across All Implementations

### Protocol
All implementations support:
- **JOIN handshake**: `JOIN <username>\n` to register username
- **Chat messages**: Any text followed by newline
- **Disconnect**: `#` to gracefully disconnect
- **Server broadcast**: Messages from server to all clients

### Signal Handling
- **SIGINT (Ctrl+C)**: Graceful shutdown
- Notifies all clients before terminating
- Cleans up resources properly

### Client Features
- Connect to server by IP address
- Username registration
- Send messages to all connected clients
- Receive messages from others
- Graceful disconnect

---

## Building and Running

### Compilation
```bash
# Navigate to any implementation directory
cd Chat-Program-Multithread/  # or Non-Blocking, Polling, Epoll

# Compile server
g++ -std=c++11 -pthread server.cpp -o server

# Compile client
g++ -std=c++11 -pthread client.cpp -o client
```

### Running
```bash
# Terminal 1: Start server
./server

# Terminal 2: Start first client
./client
# Enter server IP (e.g., 127.0.0.1)
# Enter username

# Terminal 3: Start second client
./client
# Enter server IP
# Enter username

# Chat between clients!
```

---

## Performance Comparison

| Implementation | Scalability | CPU Usage | Memory Usage | Complexity | Portability |
|---------------|-------------|-----------|--------------|------------|-------------|
| Multithread   | Low (~100)  | Medium    | High         | Low        | High        |
| Non-Blocking  | Low (~100)  | Medium    | High         | Medium     | High        |
| Poll          | Medium (~1K)| Low       | Medium       | Medium     | High        |
| Epoll         | High (10K+) | Very Low  | Low          | High       | Linux Only  |

---

## Learning Objectives

Each implementation teaches different concepts:

1. **Multithread**: Thread management, synchronization primitives
2. **Non-Blocking**: Non-blocking I/O, EAGAIN handling, polling patterns
3. **Poll**: I/O multiplexing, event-driven programming, single-threaded concurrency
4. **Epoll**: High-performance Linux networking, scalability, message buffering

---

## Technical Notes

### Socket Programming Basics
- `socket()`: Create endpoint for communication
- `bind()`: Associate socket with address
- `listen()`: Mark socket as passive (server)
- `accept()`: Accept incoming connection
- `connect()`: Initiate connection (client)
- `send()`/`recv()`: Transfer data
- `close()`: Close socket

### Important Socket Options
- `SO_REUSEADDR`: Allow quick restart of server
- `O_NONBLOCK`: Non-blocking mode
- `SHUT_RDWR`: Shutdown both read and write

### Error Codes
- `EAGAIN`/`EWOULDBLOCK`: No data available (non-blocking)
- `EINTR`: System call interrupted by signal
- `EPIPE`: Broken pipe (connection closed)

---

## Future Enhancements

Possible improvements:
- Private messaging between users
- Message history/persistence
- TLS/SSL encryption
- Authentication system
- File transfer support
- Compression
- Rate limiting
- Connection pooling
- Load balancing

---

## License

This is an educational project demonstrating different I/O models in network programming.

## Author

Chat server implementations showcasing various concurrency and I/O models in C++.