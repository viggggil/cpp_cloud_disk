#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <list>

#include <mysql/mysql.h>

#include "../lock/locker.h"

// A very small MySQL connection pool used by the metadata layer.
//
// Configuration (host/user/password/database/port) is currently
// hard-coded in sql_connection_pool.cpp for simplicity and can be
// adjusted there to match your local MySQL deployment.

class ConnectionPool {
public:
    // Singleton access
    static ConnectionPool* get_instance();

    // Lazily initialize the pool with up to max_conn connections.
    // Calling init() multiple times is safe; subsequent calls are
    // ignored once the pool has been created.
    void init(int max_conn);

    // Borrow a connection from the pool. Returns nullptr if the pool
    // could not be initialized or if no connection is available.
    MYSQL* get_connection();

    // Return a connection to the pool.
    void release_connection(MYSQL* conn);

    int max_conn() const;

private:
    ConnectionPool();
    ~ConnectionPool();

    int max_conn_ = 0;
    int cur_conn_ = 0;
    int free_conn_ = 0;

    std::list<MYSQL*> conn_list_;
    Locker lock_;
    Semaphore sem_;
};

// Simple RAII helper for borrowing a MYSQL* from the pool.
class ConnectionRAII {
public:
    ConnectionRAII(MYSQL** conn, ConnectionPool* pool);
    ~ConnectionRAII();

private:
    MYSQL* conn_ = nullptr;
    ConnectionPool* pool_ = nullptr;
};

#endif
