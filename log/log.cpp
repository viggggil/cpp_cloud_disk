#include "log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

Log* Log::get_instance() {
    static Log instance;
    return &instance;
}

void Log::init(const std::string& file_name, int close_log) {
    close_log_ = close_log;
    file_name_ = file_name;
}

void Log::write_info(const std::string& message) {
    write("INFO", message);
}

void Log::write_warn(const std::string& message) {
    write("WARN", message);
}

void Log::write_error(const std::string& message) {
    write("ERROR", message);
}

void Log::write_debug(const std::string& message) {
    write("DEBUG", message);
}

void Log::write(const char* level, const std::string& message) {
    if (close_log_) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&now_time, &tm_buf);

    std::ostringstream oss;
    oss << '[' << level << "] "
        << (tm_buf.tm_year + 1900) << '-';
    if (tm_buf.tm_mon + 1 < 10) oss << '0';
    oss << (tm_buf.tm_mon + 1) << '-';
    if (tm_buf.tm_mday < 10) oss << '0';
    oss << tm_buf.tm_mday << ' ';
    if (tm_buf.tm_hour < 10) oss << '0';
    oss << tm_buf.tm_hour << ':';
    if (tm_buf.tm_min < 10) oss << '0';
    oss << tm_buf.tm_min << ':';
    if (tm_buf.tm_sec < 10) oss << '0';
    oss << tm_buf.tm_sec << ' ' << message;

    const std::string line = oss.str();
    std::lock_guard<std::mutex> lock(mu_);
    std::cout << line << std::endl;
    if (!file_name_.empty()) {
        std::ofstream ofs(file_name_, std::ios::app);
        if (ofs.is_open()) {
            ofs << line << '\n';
        }
    }
}
