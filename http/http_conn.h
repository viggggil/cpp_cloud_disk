#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <string>

#include "http_parser.h"

class HttpConn {
public:
    static const int READ_BUFFER_SIZE = 8192;
    static const int WRITE_BUFFER_SIZE = 8192;
    static const int FILENAME_LEN = 256;
    static const int MAX_REQUEST_SIZE = 21 * 1024 * 1024; // 20MB 文件 + 一点表单/请求头余量

    HttpConn() = default;
    ~HttpConn() = default;

    void init(int sockfd, const sockaddr_in& addr);
    void close_conn(bool real_close = true);

    void process();
    bool read_once();
    bool write();

    bool has_complete_request();

    int sockfd() const { return sockfd_; }
    sockaddr_in* get_address() { return &cli_addr_; }

    static void init_epoll(int epollfd) { epollfd_ = epollfd; }
    static void init_root_path(const char* path) { root_ = path; }
    static void init_trig_mode(int listen_trig, int conn_trig) {
        listen_trig_ = listen_trig;
        conn_trig_ = conn_trig;
    }

    static int set_nonblocking(int fd);
    static void add_fd(int fd, int epollfd, bool one_shot, bool is_listen);
    static void remove_fd(int fd, int epollfd);
    static void mod_fd(int fd, int epollfd, uint32_t ev);

private:
    int sockfd_ = -1;
    sockaddr_in cli_addr_{};

    std::string read_buf_;
    long read_idx_ = 0;
    std::string response_body_;

    char write_buf_[WRITE_BUFFER_SIZE]{};
    long write_idx_ = 0;

    char file_[FILENAME_LEN]{};
    struct stat file_stat_{};
    struct iovec iov_[2]{};
    int iov_cnt_ = 0;

    bool add_response(const char* format, ...);
    bool add_file(const char* path);
    bool add_mapped_file_response(const char* path,
                                  const char* content_type,
                                  const char* disposition = nullptr);
    void unmap_file();
    void* file_addr_ = nullptr;
    long file_size_ = 0;

    static int epollfd_;
    static const char* root_;
    static int listen_trig_;
    static int conn_trig_;

    bool parse_request();
    void prepare_error(int code, const char* msg);
    void prepare_json(int code, const char* body);
    void prepare_binary(int code,
                        const char* status,
                        const char* content_type,
                        const char* body,
                        size_t body_len,
                        const char* disposition = nullptr);
    bool current_user_from_request(const HttpRequest& req, std::string& user);
    bool handle_api_request(const HttpRequest& req);
};

#endif
