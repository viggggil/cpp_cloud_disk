#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

class ConnectionPool {
public:
    static ConnectionPool* get_instance();
    void init(int max_conn);
    int max_conn() const;

private:
    ConnectionPool() = default;
    int max_conn_ = 0;
};

#endif
