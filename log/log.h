#ifndef LOG_H
#define LOG_H

#include <string>

class Log {
public:
    static Log* get_instance();
    void init(const std::string& file_name, int close_log);
    void write_info(const std::string& message);

private:
    Log() = default;
    int close_log_ = 0;
};

#endif
