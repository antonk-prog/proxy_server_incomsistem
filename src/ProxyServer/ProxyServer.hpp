#pragma once
#include <memory>
#include <queue>
#include <string>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <unordered_map>

#include "Parser.hpp"

#define BUFFER_SIZE 8192
#define MAX_EVENTS 1024



struct Buffer {
    std::vector<char> data;
    size_t offset = 0;

    bool empty() const {
        return offset >= data.size();
    }

    size_t size() const {
        return data.size() - offset;
    }

    const char* ptr() const {
        return data.data() + offset;
    }

    void consume(size_t n) {
        offset += n;
        if (offset >= data.size()) {
            clear();
        }
    }

    void append(const char* src, size_t n) {
        data.insert(data.end(), src, src + n);
    }

    void clear() {
        data.clear();
        offset = 0;
    }
};

struct ProxyConnection {
	int client_fd;
	int server_fd;
	Buffer client_buf;
	Buffer server_buf; 
};

struct Worker {
    int epoll_fd;
    std::thread thread;
    std::mutex mutex;
    std::queue<std::pair<int, int>> new_connections; // (client_fd, server_fd)
    std::condition_variable cv;
    std::unordered_map<int, ProxyConnection> connections;
    std::unordered_map<int, int> fd_to_owner;

    void updateEvents(int socket, const Buffer& out_buf) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    if (!out_buf.empty()) ev.events |= EPOLLOUT;
    ev.data.fd = socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, socket, &ev) < 0) {
        if (errno == ENOENT) {
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket, &ev);
        } else {
            perror("epoll_ctl MOD failed");
        }
    }
}
};

class ProxyServer {
private:
    int pg_port;
    std::string pg_host;

    int listen_port;
    int listen_fd;

    int epoll_fd;
    epoll_event ev{};
    epoll_event events[MAX_EVENTS];

    std::unordered_map<int, ProxyConnection> connections;
    std::unordered_map<int, int> fd_to_owner;
    std::vector<std::unique_ptr<Worker>> workers;
    int next_worker = 0;
    int num_threads = 6;
    Parser* parser = nullptr;
public:
    ProxyServer(int argc, char *argv[]);

    void run();
    void attachParser(Parser * parser);


private:
    void workerLoop(Worker* worker);
    void initializeServer(int argc, char *argv[]);
    void setNonblocking(int fd);
    void acceptNewConnections();
    int connectToPg();
    bool handleReadEvent(Worker* worker, int fd, ProxyConnection& conn);
    bool handleWriteEvent(Worker* worker, int fd, ProxyConnection& conn);
    void closeConnection(Worker* worker, ProxyConnection& conn);
};