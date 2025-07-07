#include <iostream>
#include <stdexcept>
#include <system_error>
#include <cerrno>

#include "ProxyServer.hpp"



ProxyServer::ProxyServer(int argc, char *argv[]) {
    initializeServer(argc, argv);
}

void ProxyServer::initializeServer(int argc, char *argv[]){
    if (argc != 4) {
        throw std::invalid_argument("Usage: ./pg_proxy <listen_port> <pg_host> <pg_port>\n");
	}

    listen_port = atoi(argv[1]);
    pg_host = argv[2];
	pg_port = atoi(argv[3]);

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);

	sockaddr_in listen_addr{};
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(listen_port);

	if (bind(listen_fd, (sockaddr *)&listen_addr, sizeof(listen_addr))<0){
        throw std::system_error(errno, std::system_category(), "bind() failed");
    }

	if (listen(listen_fd, SOMAXCONN) < 0) {
        throw std::runtime_error("Failed to listen");
    }

	setNonblocking(listen_fd);

	epoll_fd = epoll_create1(0);
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

    for (int i = 0; i < num_threads; ++i) {
        auto worker = std::make_unique<Worker>();
        worker->epoll_fd = epoll_create1(0);
        worker->thread = std::thread([this, w = worker.get()]() {
            workerLoop(w); 
        });
        workers.push_back(std::move(worker));
    }
}
void ProxyServer::run() {
    std::cout << "Сервер запущен!" << std::endl;
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            throw std::system_error(errno, std::system_category(), "epoll_wait() failed");
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listen_fd) {
                acceptNewConnections();
            }
        }
    }
}
void ProxyServer::attachParser(Parser * parser) {
    if (parser == nullptr) {
        throw std::invalid_argument("Parser is nullptr");
    }
    this->parser = parser;
}

void ProxyServer::workerLoop(Worker* worker) {
    epoll_event events[MAX_EVENTS];

    while (true) {
        {
            std::unique_lock<std::mutex> lock(worker->mutex);
            while (!worker->new_connections.empty()) {
                auto [client_fd, server_fd] = worker->new_connections.front();
                worker->new_connections.pop();

                ProxyConnection conn{client_fd, server_fd};
                worker->connections[client_fd] = conn;
                worker->fd_to_owner[client_fd] = client_fd;
                worker->fd_to_owner[server_fd] = client_fd;

                epoll_event ev1;
                ev1.events = EPOLLIN | EPOLLET;
                ev1.data.fd = client_fd;

                epoll_event ev2;
                ev2.events = EPOLLIN | EPOLLET;
                ev2.data.fd = server_fd;

                epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev1);
                epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, server_fd, &ev2);
            }
        }

        int nfds = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, 100);
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            auto it = worker->fd_to_owner.find(fd);
            if (it == worker->fd_to_owner.end()) continue;

            int conn_key = it->second;
            auto& conn = worker->connections[conn_key];

            if (events[i].events & EPOLLIN) {
                if (handleReadEvent(worker, fd, conn)) {
                    closeConnection(worker, conn);
                    continue;
                }
            }
            if (events[i].events & EPOLLOUT) {
                if (handleWriteEvent(worker, fd, conn)) {
                    closeConnection(worker, conn);
                    continue;
                }
            }
        }
    }
}

void ProxyServer::setNonblocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}



void ProxyServer::acceptNewConnections() {
    int client_fd = accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        throw std::system_error(errno, std::system_category(), "accept() failed");
    }

    setNonblocking(client_fd);
    int server_fd = connectToPg();
    setNonblocking(server_fd);

    auto& worker = workers[next_worker];
    next_worker = (next_worker + 1) % workers.size();

    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->new_connections.emplace(client_fd, server_fd);
    }
    worker->cv.notify_one();
}
int ProxyServer::connectToPg() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(pg_port);

    inet_pton(AF_INET, pg_host.c_str(), &addr.sin_addr);

   	if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
        throw std::system_error(errno, std::system_category(), "connect() failed");
	}

    return fd;
}

bool ProxyServer::handleReadEvent(Worker* worker, int fd, ProxyConnection & conn) {
    char buffer[BUFFER_SIZE];
    bool closed = false;

    while (true) {
        ssize_t len = recv(fd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closed = true;
            break;
        } else if (len == 0) {
            closed = true;
            break;
        } else {
            if (fd == conn.client_fd) {
                if (parser) {
                    parser->parseClientMessage(buffer, len);
                }
                conn.client_buf.append(buffer, len);
                worker->updateEvents(conn.server_fd, conn.client_buf);
            } else if (fd == conn.server_fd) {
                conn.server_buf.append(buffer, len);
                worker->updateEvents(conn.client_fd, conn.server_buf);
            }
        }
    }

    if (closed) {
        closeConnection(worker, conn);
        return true;
    }
    return false;
}

bool ProxyServer::handleWriteEvent(Worker* worker, int fd, ProxyConnection& conn) {
    bool closed = false;

    if (fd == conn.server_fd && !conn.client_buf.empty()) {
        while (!conn.client_buf.empty()) {
            ssize_t sent = send(conn.server_fd, conn.client_buf.ptr(), conn.client_buf.size(), 0);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                closed = true;
                break;
            }
            conn.client_buf.consume(sent);
        }
        worker->updateEvents(conn.server_fd, conn.client_buf);
    }

    if (fd == conn.client_fd && !conn.server_buf.empty()) {
        while (!conn.server_buf.empty()) {
            ssize_t sent = send(conn.client_fd, conn.server_buf.ptr(), conn.server_buf.size(), 0);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                closed = true;
                break;
            }
            conn.server_buf.consume(sent);
        }
        worker->updateEvents(conn.client_fd, conn.server_buf);
    }

    if (closed) {
        closeConnection(worker, conn);
        return true;
    }
    return false;
}


void ProxyServer::closeConnection(Worker* worker, ProxyConnection& conn) {
    epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, conn.client_fd, nullptr);
    epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, conn.server_fd, nullptr);

    close(conn.client_fd);
    close(conn.server_fd);

    worker->fd_to_owner.erase(conn.client_fd);
    worker->fd_to_owner.erase(conn.server_fd);
    worker->connections.erase(conn.client_fd);
}
