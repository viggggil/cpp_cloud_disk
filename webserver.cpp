#include "webserver.h"
#include "CGImysql/metadata_store.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr int MAX_FD = 65536;
constexpr int MAX_EVENT_NUMBER = 10000;

}  // namespace

WebServer::WebServer()
    : port_(9006),
      trig_mode_(0),
      sql_num_(8),
      thread_num_(8),
      close_log_(0),
      actor_model_(0),
      listen_trig_(0),
      conn_trig_(0),
      listenfd_(-1),
      epollfd_(-1),
      pool_(nullptr),
      users_(nullptr) {}

WebServer::~WebServer() {
    delete pool_;
    pool_ = nullptr;
    delete[] users_;
    users_ = nullptr;
    if (listenfd_ >= 0) {
        close(listenfd_);
        listenfd_ = -1;
    }
    if (epollfd_ >= 0) {
        close(epollfd_);
        epollfd_ = -1;
    }
}

void WebServer::init(int port, int trig_mode, int sql_num, int thread_num, int close_log, int actor_model) {
    (void)actor_model;
    port_ = port;
    trig_mode_ = trig_mode;
    sql_num_ = sql_num;
    thread_num_ = thread_num;
    close_log_ = close_log;
    actor_model_ = actor_model;
}

void WebServer::log_write() {
    if (close_log_) {
        return;
    }
    std::printf("[log] init, close_log=%d\n", close_log_);
}

void WebServer::sql_pool() {
    // Initialize MySQL connection pool and DB schema.
    (void)sql_num_;
    if (!MetadataStore::instance().init_schema()) {
        std::fprintf(stderr, "[db] init_schema failed. Check MySQL settings in sql_connection_pool.cpp and ensure database exists.\n");
    }
}

void WebServer::thread_pool() {
    pool_ = new ThreadPool<HttpConn>(thread_num_);
}

void WebServer::trig_mode() {
    switch (trig_mode_) {
    case 0:
        listen_trig_ = 0;
        conn_trig_ = 0;
        break;
    case 1:
        listen_trig_ = 0;
        conn_trig_ = 1;
        break;
    case 2:
        listen_trig_ = 1;
        conn_trig_ = 0;
        break;
    case 3:
        listen_trig_ = 1;
        conn_trig_ = 1;
        break;
    default:
        listen_trig_ = 0;
        conn_trig_ = 0;
        break;
    }
    HttpConn::init_trig_mode(listen_trig_, conn_trig_);
}

void WebServer::event_listen() {
    users_ = new HttpConn[MAX_FD];

    listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd_ < 0) {
        throw std::runtime_error("socket() failed");
    }

    int opt = 1;
    setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    HttpConn::set_nonblocking(listenfd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listenfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(listenfd_);
        listenfd_ = -1;
        throw std::runtime_error("bind() failed");
    }

    if (listen(listenfd_, 2048) < 0) {
        close(listenfd_);
        listenfd_ = -1;
        throw std::runtime_error("listen() failed");
    }

    epollfd_ = epoll_create(128);
    if (epollfd_ < 0) {
        close(listenfd_);
        listenfd_ = -1;
        throw std::runtime_error("epoll_create() failed");
    }

    HttpConn::init_epoll(epollfd_);
    HttpConn::init_root_path("./root");

    HttpConn::add_fd(listenfd_, epollfd_, false, true);
}

void WebServer::deal_listen() {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    while (true) {
        int connfd = accept(listenfd_, reinterpret_cast<sockaddr*>(&addr), &len);
        if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }
        if (connfd >= MAX_FD) {
            close(connfd);
            continue;
        }
        HttpConn::set_nonblocking(connfd);
        HttpConn::add_fd(connfd, epollfd_, true, false);
        users_[connfd].init(connfd, addr);
    }
}

void WebServer::deal_read(int sockfd) {
    if (sockfd == listenfd_) {
        return;
    }
    if (!users_[sockfd].read_once()) {
        users_[sockfd].close_conn();
        return;
    }
    if (!users_[sockfd].has_complete_request()) {
        HttpConn::mod_fd(sockfd, epollfd_, EPOLLIN);
        return;
    }
    if (!pool_->append(&users_[sockfd])) {
        users_[sockfd].close_conn();
    }
}

void WebServer::deal_write(int sockfd) {
    if (sockfd == listenfd_) {
        return;
    }
    if (!users_[sockfd].write()) {
        users_[sockfd].close_conn();
    }
}

void WebServer::event_loop() {
    epoll_event events[MAX_EVENT_NUMBER];
    while (true) {
        int num = epoll_wait(epollfd_, events, MAX_EVENT_NUMBER, -1);
        if (num < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd_) {
                deal_listen();
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users_[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                deal_read(sockfd);
            } else if (events[i].events & EPOLLOUT) {
                deal_write(sockfd);
            }
        }
    }
}
