#include "sql_connection_pool.h"

ConnectionPool* ConnectionPool::get_instance() {
    static ConnectionPool instance;
    return &instance;
}

void ConnectionPool::init(int max_conn) { max_conn_ = max_conn; }

int ConnectionPool::max_conn() const { return max_conn_; }
