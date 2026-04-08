#include "config.h"
#include "webserver.h"

#include <cstdio>
#include <exception>

int main(int argc, char* argv[]) {
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;
    server.init(config.port, config.trig_mode, config.sql_num, config.thread_num, config.close_log, config.actor_model);
    server.log_write();
    server.sql_pool();
    server.thread_pool();
    server.trig_mode();
    try {
        server.event_listen();
        server.event_loop();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    return 0;
}
