#ifndef LOG_H
#define LOG_H

#include <mutex>
#include <string>

class Log {
public:
    static Log* get_instance();
    void init(const std::string& file_name, int close_log);
    void write_info(const std::string& message);
    void write_warn(const std::string& message);
    void write_error(const std::string& message);
    void write_debug(const std::string& message);

private:
    Log() = default;
    void write(const char* level, const std::string& message);

    int close_log_ = 0;
    std::string file_name_;
    std::mutex mu_;
};

#endif
