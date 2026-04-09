#include "http_conn.h"
#include "auth_manager.h"
#include "file_service.h"
#include "json_utils.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>

int HttpConn::epollfd_ = -1;
const char* HttpConn::root_ = "./root";
int HttpConn::listen_trig_ = 0;
int HttpConn::conn_trig_ = 0;

namespace {
std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    return out;
}

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = {s[i + 1], s[i + 2], '\0'};
            char* end = nullptr;
            long v = std::strtol(hex, &end, 16);
            if (end != hex) {
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool parse_query_param(const std::string& path, const std::string& key, std::string& value) {
    const size_t qpos = path.find('?');
    if (qpos == std::string::npos) return false;
    const std::string query = path.substr(qpos + 1);
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        const std::string part = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t eq = part.find('=');
        const std::string k = part.substr(0, eq);
        const std::string v = eq == std::string::npos ? "" : part.substr(eq + 1);
        if (k == key) {
            value = url_decode(v);
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

bool query_param_is_true(const std::string& path, const std::string& key) {
    std::string value;
    if (!parse_query_param(path, key, value)) return false;
    return value == "1" || value == "true" || value == "yes";
}

bool parse_multipart_file(const HttpRequest& req,
                          std::string& file_path,
                          std::vector<unsigned char>& file_data,
                          std::string& err) {
    const std::string content_type = HttpParser::get_header(req, "Content-Type");
    const std::string boundary_key = "boundary=";
    const size_t boundary_pos = content_type.find(boundary_key);
    if (boundary_pos == std::string::npos) {
        err = "missing multipart boundary";
        return false;
    }
    const std::string boundary = "--" + content_type.substr(boundary_pos + boundary_key.size());
    if (boundary == "--") {
        err = "invalid multipart boundary";
        return false;
    }

    const std::string& body = req.body;
    size_t pos = 0;
    while (true) {
        size_t part_start = body.find(boundary, pos);
        if (part_start == std::string::npos) break;
        part_start += boundary.size();
        if (part_start + 1 < body.size() && body.compare(part_start, 2, "--") == 0) {
            break;
        }
        if (part_start + 2 <= body.size() && body.compare(part_start, 2, "\r\n") == 0) {
            part_start += 2;
        }

        const size_t header_end = body.find("\r\n\r\n", part_start);
        if (header_end == std::string::npos) {
            err = "invalid multipart body";
            return false;
        }
        const std::string header_block = body.substr(part_start, header_end - part_start);
        const size_t data_start = header_end + 4;
        const size_t next_boundary = body.find(boundary, data_start);
        if (next_boundary == std::string::npos) {
            err = "invalid multipart ending";
            return false;
        }
        size_t data_end = next_boundary;
        if (data_end >= 2 && body.compare(data_end - 2, 2, "\r\n") == 0) {
            data_end -= 2;
        }

        const bool is_file_part = header_block.find("name=\"file\"") != std::string::npos;
        const bool is_path_part = header_block.find("name=\"path\"") != std::string::npos;

        if (is_path_part) {
            file_path = body.substr(data_start, data_end - data_start);
        } else if (is_file_part) {
            size_t fn = header_block.find("filename=\"");
            if (fn != std::string::npos) {
                fn += 10;
                size_t fn_end = header_block.find('"', fn);
                if (fn_end != std::string::npos && file_path.empty()) {
                    file_path = header_block.substr(fn, fn_end - fn);
                }
            }
            file_data.assign(body.begin() + static_cast<std::ptrdiff_t>(data_start),
                             body.begin() + static_cast<std::ptrdiff_t>(data_end));
        }

        pos = next_boundary;
    }

    if (file_path.empty()) {
        err = "missing file path";
        return false;
    }
    if (file_data.empty()) {
        err = "empty file or missing file field";
        return false;
    }
    return true;
}
}  // namespace

int HttpConn::set_nonblocking(int fd) {
    int old = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old | O_NONBLOCK);
    return old;
}

void HttpConn::add_fd(int fd, int epollfd, bool one_shot, bool is_listen) {
    epoll_event event{};
    event.data.fd = fd;
    int trig = is_listen ? listen_trig_ : conn_trig_;
    if (trig) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

void HttpConn::remove_fd(int fd, int epollfd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
}

void HttpConn::mod_fd(int fd, int epollfd, uint32_t ev) {
    epoll_event event{};
    event.data.fd = fd;
    if (conn_trig_) {
        event.events = ev | EPOLLET | EPOLLONESHOT;
    } else {
        event.events = ev | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void HttpConn::unmap_file() {
    if (file_addr_ != nullptr && file_addr_ != MAP_FAILED) {
        munmap(file_addr_, static_cast<size_t>(file_size_));
        file_addr_ = nullptr;
        file_size_ = 0;
    }
}

void HttpConn::init(int sockfd, const sockaddr_in& addr) {
    sockfd_ = sockfd;
    cli_addr_ = addr;
    read_buf_.clear();
    read_buf_.reserve(READ_BUFFER_SIZE);
    read_idx_ = 0;
    write_idx_ = 0;
    iov_cnt_ = 0;
    unmap_file();
}

void HttpConn::close_conn(bool real_close) {
    if (real_close && sockfd_ >= 0) {
        unmap_file();
        remove_fd(sockfd_, epollfd_);
        close(sockfd_);
        sockfd_ = -1;
    }
}

bool HttpConn::read_once() {
    char buf[READ_BUFFER_SIZE];
    while (true) {
        long bytes_read = recv(sockfd_, buf, sizeof(buf), 0);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        if (bytes_read == 0) {
            return false;
        }
        if (read_buf_.size() + static_cast<size_t>(bytes_read) > static_cast<size_t>(MAX_REQUEST_SIZE)) {
            return false;
        }
        read_buf_.append(buf, static_cast<size_t>(bytes_read));
        read_idx_ += bytes_read;
        if (conn_trig_ == 0) {
            break;
        }
    }
    return true;
}

bool HttpConn::has_complete_request() {
    if (read_idx_ < 4) {
        return false;
    }
    return HttpParser::has_complete_request(read_buf_.data(), read_idx_);
}

bool HttpConn::add_response(const char* format, ...) {
    if (write_idx_ >= WRITE_BUFFER_SIZE - 1) {
        return false;
    }
    va_list args;
    va_start(args, format);
    int len = vsnprintf(write_buf_ + write_idx_, WRITE_BUFFER_SIZE - write_idx_ - 1, format, args);
    va_end(args);
    if (len < 0 || write_idx_ + len >= WRITE_BUFFER_SIZE - 1) {
        return false;
    }
    write_idx_ += len;
    return true;
}

void HttpConn::prepare_error(int code, const char* msg) {
    unmap_file();
    char body[512];
    snprintf(body, sizeof body, "<html><body><h1>%d</h1><p>%s</p></body></html>", code, msg);

    write_idx_ = 0;
    add_response("HTTP/1.1 %d %s\r\n", code, msg);
    add_response("Content-Type: text/html; charset=utf-8\r\n");
    add_response("Content-Length: %zu\r\n", strlen(body));
    add_response("Connection: close\r\n\r\n");
    add_response("%s", body);

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = static_cast<size_t>(write_idx_);
    iov_cnt_ = 1;
}

void HttpConn::prepare_json(int code, const char* body) {
    write_idx_ = 0;
    add_response("HTTP/1.1 %d OK\r\n", code);
    add_response("Content-Type: application/json; charset=utf-8\r\n");
    add_response("Content-Length: %zu\r\n", strlen(body));
    add_response("Connection: close\r\n\r\n");
    add_response("%s", body);

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = static_cast<size_t>(write_idx_);
    iov_cnt_ = 1;
}

void HttpConn::prepare_binary(int code,
                              const char* status,
                              const char* content_type,
                              const char* body,
                              size_t body_len,
                              const char* disposition) {
    write_idx_ = 0;
    add_response("HTTP/1.1 %d %s\r\n", code, status);
    add_response("Content-Type: %s\r\n", content_type ? content_type : "application/octet-stream");
    if (disposition && *disposition) {
        add_response("Content-Disposition: %s\r\n", disposition);
    }
    add_response("Content-Length: %zu\r\n", body_len);
    add_response("Connection: close\r\n\r\n");

    if (body_len > 0 && body != nullptr) {
        const size_t header_len = static_cast<size_t>(write_idx_);
        if (header_len + body_len > WRITE_BUFFER_SIZE) {
            prepare_error(500, "Response Too Large");
            return;
        }
        std::memcpy(write_buf_ + write_idx_, body, body_len);
        write_idx_ += static_cast<long>(body_len);
    }

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = static_cast<size_t>(write_idx_);
    iov_cnt_ = 1;
}

bool HttpConn::add_file(const char* path) {
    unmap_file();

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        prepare_error(404, "Not Found");
        return true;
    }
    if (fstat(fd, &file_stat_) < 0 || !S_ISREG(file_stat_.st_mode)) {
        close(fd);
        prepare_error(404, "Not Found");
        return true;
    }

    file_size_ = file_stat_.st_size;
    file_addr_ = mmap(nullptr, static_cast<size_t>(file_size_), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_addr_ == MAP_FAILED) {
        file_addr_ = nullptr;
        file_size_ = 0;
        prepare_error(500, "Internal Server Error");
        return true;
    }

    write_idx_ = 0;
    add_response("HTTP/1.1 200 OK\r\n");
    add_response("Content-Length: %ld\r\n", file_size_);
    add_response("Connection: close\r\n\r\n");

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = static_cast<size_t>(write_idx_);
    iov_[1].iov_base = file_addr_;
    iov_[1].iov_len = static_cast<size_t>(file_size_);
    iov_cnt_ = 2;
    return true;
}

bool HttpConn::add_mapped_file_response(const char* path,
                                        const char* content_type,
                                        const char* disposition) {
    unmap_file();

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        prepare_error(404, "Not Found");
        return true;
    }
    if (fstat(fd, &file_stat_) < 0 || !S_ISREG(file_stat_.st_mode)) {
        close(fd);
        prepare_error(404, "Not Found");
        return true;
    }

    file_size_ = file_stat_.st_size;
    file_addr_ = mmap(nullptr, static_cast<size_t>(file_size_), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_addr_ == MAP_FAILED) {
        file_addr_ = nullptr;
        file_size_ = 0;
        prepare_error(500, "Internal Server Error");
        return true;
    }

    write_idx_ = 0;
    add_response("HTTP/1.1 200 OK\r\n");
    add_response("Content-Type: %s\r\n", content_type ? content_type : "application/octet-stream");
    if (disposition && *disposition) {
        add_response("Content-Disposition: %s\r\n", disposition);
    }
    add_response("Content-Length: %ld\r\n", file_size_);
    add_response("Connection: close\r\n\r\n");

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = static_cast<size_t>(write_idx_);
    iov_[1].iov_base = file_addr_;
    iov_[1].iov_len = static_cast<size_t>(file_size_);
    iov_cnt_ = 2;
    return true;
}

bool HttpConn::handle_api_request(const HttpRequest& req) {
    std::unordered_map<std::string, std::string> json;
    if (req.method == "POST" && req.path.rfind("/api/", 0) == 0 && req.path != "/api/files/upload-local") {
        if (!JsonUtils::parse_flat_object(req.body, json)) {
            prepare_error(400, "Bad Request");
            return true;
        }
    }

    if (req.path == "/api/auth/register" && req.method == "POST") {
        const std::string username = JsonUtils::get(json, "username");
        const std::string password = JsonUtils::get(json, "password");
        if (!AuthManager::instance().register_user(username, password)) {
            prepare_error(409, "Conflict");
            return true;
        }
        prepare_json(200, "{\"ok\":true,\"message\":\"registered\"}");
        return true;
    }

    if (req.path == "/api/auth/login" && req.method == "POST") {
        const std::string username = JsonUtils::get(json, "username");
        const std::string password = JsonUtils::get(json, "password");
        if (!AuthManager::instance().verify_user(username, password)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        // Always use cookie-based session; ignore JWT mode.
        const int ttl_seconds = 3600;  // 1 hour
        const std::string sid = AuthManager::instance().issue_session(username, ttl_seconds);

        write_idx_ = 0;
        add_response("HTTP/1.1 200 OK\r\n");
        add_response("Set-Cookie: sid=%s; HttpOnly; Max-Age=%d; Path=/\r\n", sid.c_str(), ttl_seconds);
        add_response("Content-Type: application/json; charset=utf-8\r\n");
        const char* body = "{\"ok\":true,\"auth\":\"session\"}";
        add_response("Content-Length: %zu\r\n", strlen(body));
        add_response("Connection: close\r\n\r\n");
        add_response("%s", body);

        iov_[0].iov_base = write_buf_;
        iov_[0].iov_len = static_cast<size_t>(write_idx_);
        iov_cnt_ = 1;
        return true;
    }

    if (req.path == "/api/me" && req.method == "GET") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        char resp[512];
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"username\":\"%s\"}", username.c_str());
        prepare_json(200, resp);
        return true;
    }

    if (req.path == "/api/files/upload" && req.method == "POST") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        const std::string path = JsonUtils::get(json, "path");
        const std::string content = JsonUtils::get(json, "content");
        std::string err;
        if (!FileService::upload_text_file(username, path, content, err)) {
            prepare_error(400, err.c_str());
            return true;
        }
        prepare_json(200, "{\"ok\":true}");
        return true;
    }

    if (req.path == "/api/files/upload-local" && req.method == "POST") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        std::string rel_path;
        std::vector<unsigned char> data;
        std::string err;
        if (!parse_multipart_file(req, rel_path, data, err)) {
            prepare_error(400, err.c_str());
            return true;
        }
        if (!FileService::upload_binary_file(username, rel_path, data, err)) {
            prepare_error(400, err.c_str());
            return true;
        }
        prepare_json(200, "{\"ok\":true}");
        return true;
    }

    if (req.path == "/api/files/download" && req.method == "POST") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        const std::string path = JsonUtils::get(json, "path");
        std::string content;
        std::string err;
        if (!FileService::download_text_file(username, path, content, err)) {
            prepare_error(404, err.c_str());
            return true;
        }
        const std::string resp = std::string("{\"ok\":true,\"path\":\"") +
                                 escape_json(path) +
                                 "\",\"content\":\"" +
                                 escape_json(content) +
                                 "\"}";
        prepare_json(200, resp.c_str());
        return true;
    }

    if (req.path.rfind("/api/files/raw-download", 0) == 0 && req.method == "GET") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        std::string rel_path;
        if (!parse_query_param(req.path, "path", rel_path) || rel_path.empty()) {
            prepare_error(400, "missing path");
            return true;
        }
        std::vector<unsigned char> data;
        std::string err;
        std::string full_path;
        if (!FileService::read_file_binary(username, rel_path, data, err, &full_path)) {
            prepare_error(404, err.c_str());
            return true;
        }
        const std::string content_type = FileService::guess_content_type(rel_path);
        const bool preview_mode = query_param_is_true(req.path, "preview");
        char disposition[512];
        if (preview_mode) {
            return add_mapped_file_response(full_path.c_str(), content_type.c_str(), nullptr);
        }
        std::snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", rel_path.c_str());
        return add_mapped_file_response(full_path.c_str(), content_type.c_str(), disposition);
    }

    if (req.path == "/api/files/delete" && req.method == "POST") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        std::string err;
        if (!FileService::delete_file(username, JsonUtils::get(json, "path"), err)) {
            prepare_error(400, err.c_str());
            return true;
        }
        prepare_json(200, "{\"ok\":true}");
        return true;
    }

    if (req.path == "/api/files/rename" && req.method == "POST") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        std::string err;
        if (!FileService::rename_file(username, JsonUtils::get(json, "old_path"), JsonUtils::get(json, "new_path"), err)) {
            prepare_error(400, err.c_str());
            return true;
        }
        prepare_json(200, "{\"ok\":true}");
        return true;
    }

    if (req.path == "/api/files/list" && req.method == "POST") {
        std::string username;
        if (!current_user_from_request(req, username)) {
            prepare_error(401, "Unauthorized");
            return true;
        }
        const std::string rel_dir = JsonUtils::get(json, "dir");
        const int page = JsonUtils::get_int(json, "page", 1);
        const int page_size = JsonUtils::get_int(json, "page_size", 20);
        const std::string result = FileService::list_files_json(username, rel_dir, page, page_size);
        prepare_json(200, result.c_str());
        return true;
    }

    return false;
}

bool HttpConn::current_user_from_request(const HttpRequest& req, std::string& user) {
    const std::string cookie = HttpParser::get_header(req, "Cookie");
    const std::string key = "sid=";
    size_t pos = cookie.find(key);
    if (pos == std::string::npos) {
        return false;
    }
    size_t end = cookie.find(';', pos);
    std::string sid = cookie.substr(pos + key.size(), end == std::string::npos ? std::string::npos : end - (pos + key.size()));
    return AuthManager::instance().verify_session(sid, user);
}

bool HttpConn::parse_request() {
    HttpRequest req;
    if (!HttpParser::parse(read_buf_.data(), read_idx_, req)) {
        prepare_error(400, "Bad Request");
        return true;
    }

    if (handle_api_request(req)) {
        return true;
    }

    if (req.method != "GET") {
        prepare_error(501, "Not Implemented");
        return true;
    }

    if (req.path.empty() || req.path[0] != '/') {
        prepare_error(400, "Bad Request");
        return true;
    }
    if (req.path.find("..") != std::string::npos) {
        prepare_error(403, "Forbidden");
        return true;
    }

    if (req.path == "/") {
        snprintf(file_, sizeof file_, "%s/index.html", root_);
    } else {
        snprintf(file_, sizeof file_, "%s%s", root_, req.path.c_str());
    }
    return add_file(file_);
}

void HttpConn::process() {
    if (!has_complete_request()) {
        mod_fd(sockfd_, epollfd_, EPOLLIN);
        return;
    }

    parse_request();
    mod_fd(sockfd_, epollfd_, EPOLLOUT);
}

bool HttpConn::write() {
    if (iov_cnt_ <= 0) {
        return false;
    }

    while (true) {
        ssize_t n = writev(sockfd_, iov_, iov_cnt_);
        if (n < 0) {
            if (errno == EAGAIN) {
                mod_fd(sockfd_, epollfd_, EPOLLOUT);
                return true;
            }
            unmap_file();
            return false;
        }
        if (n == 0) {
            continue;
        }

        size_t remaining = static_cast<size_t>(n);
        while (remaining > 0 && iov_cnt_ > 0) {
            if (remaining >= iov_[0].iov_len) {
                remaining -= iov_[0].iov_len;
                if (iov_cnt_ == 2) {
                    iov_[0] = iov_[1];
                    iov_cnt_ = 1;
                } else {
                    iov_cnt_ = 0;
                }
            } else {
                iov_[0].iov_base = static_cast<char*>(iov_[0].iov_base) + remaining;
                iov_[0].iov_len -= remaining;
                remaining = 0;
            }
        }

        if (iov_cnt_ == 0) {
            unmap_file();
            close_conn(true);
            return true;
        }
    }
}
