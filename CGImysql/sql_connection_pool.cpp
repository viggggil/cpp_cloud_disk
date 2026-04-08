#include "sql_connection_pool.h"

#include <cassert>
#include <cstdio>

ConnectionPool::ConnectionPool() : max_conn_(0), cur_conn_(0), free_conn_(0), sem_(0) {}

ConnectionPool::~ConnectionPool() {
    lock_.lock();
    for (auto* conn : conn_list_) {
        mysql_close(conn);
    }
    conn_list_.clear();
    lock_.unlock();
}

ConnectionPool* ConnectionPool::get_instance() {
    static ConnectionPool instance;
    return &instance;
}

void ConnectionPool::init(int max_conn) {
    if (max_conn <= 0) {
        max_conn = 1;
    }

    lock_.lock();
    if (max_conn_ > 0) {
        // Already initialized.
        lock_.unlock();
        return;
    }

    // TODO: adjust these to match your local MySQL instance.
    const char* host = "127.0.0.1";
    const char* user = "root";
    const char* password = "root";
    const char* dbname = "cloud_disk";
    unsigned int port = 3306;

    for (int i = 0; i < max_conn; ++i) {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            std::fprintf(stderr, "mysql_init failed\n");
            continue;
        }
        if (!mysql_real_connect(conn, host, user, password, dbname, port, nullptr, 0)) {
            std::fprintf(stderr, "mysql_real_connect failed: %s\n", mysql_error(conn));
            mysql_close(conn);
            continue;
        }
        conn_list_.push_back(conn);
        ++free_conn_;
    }

    max_conn_ = free_conn_;
    cur_conn_ = 0;
    for (int i = 0; i < free_conn_; ++i) {
        sem_.post();
    }

    lock_.unlock();
}

MYSQL* ConnectionPool::get_connection() {
    MYSQL* conn = nullptr;
    if (free_conn_ == 0) {
        return nullptr;
    }

    sem_.wait();
    lock_.lock();

    if (!conn_list_.empty()) {
        conn = conn_list_.front();
        conn_list_.pop_front();
        --free_conn_;
        ++cur_conn_;
    }

    lock_.unlock();
    return conn;
}

void ConnectionPool::release_connection(MYSQL* conn) {
    if (!conn) return;

    lock_.lock();
    conn_list_.push_back(conn);
    ++free_conn_;
    --cur_conn_;
    lock_.unlock();
    sem_.post();
}

int ConnectionPool::max_conn() const { return max_conn_; }

ConnectionRAII::ConnectionRAII(MYSQL** conn, ConnectionPool* pool) : pool_(pool) {
    assert(conn);
    *conn = pool_->get_connection();
    conn_ = *conn;
}

ConnectionRAII::~ConnectionRAII() {
    if (conn_) {
        pool_->release_connection(conn_);
    }
}
