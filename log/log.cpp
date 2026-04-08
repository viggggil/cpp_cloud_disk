#include "log.h"

#include <iostream>

Log* Log::get_instance() {
    static Log instance;
    return &instance;
}

void Log::init(const std::string& file_name, int close_log) {
    (void)file_name;
    close_log_ = close_log;
}

void Log::write_info(const std::string& message) {
    if (!close_log_) {
        std::cout << "[info] " << message << std::endl;
    }
}
