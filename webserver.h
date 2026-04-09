#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "http/http_conn.h"
#include "threadpool/threadpool.h"
#include "timer/lst_timer.h"

class WebServer {
public:
    static constexpr int MAX_FD = 65536;
    static constexpr int MAX_EVENT_NUMBER = 10000;

    WebServer();
    ~WebServer();

    void init(int port, int trig_mode, int sql_num, int thread_num, int close_log, int actor_model);
    void log_write();
    void sql_pool();
    void thread_pool();
    void trig_mode();
    void event_listen();
    void event_loop();

private:
    void close_conn(int sockfd, bool from_timer);

    void deal_listen();
    void deal_read(int sockfd);
    void deal_write(int sockfd);

    int port_;
    int trig_mode_;
    int sql_num_;
    int thread_num_;
    int close_log_;
    int actor_model_;

    int listen_trig_;
    int conn_trig_;

    int listenfd_;
    int epollfd_;

    ThreadPool<HttpConn>* pool_;
    HttpConn* users_;

    TimerList timer_list_;
    UtilTimer* conn_timers_[MAX_FD];
};

#endif
