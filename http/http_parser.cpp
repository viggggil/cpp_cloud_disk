#include "http_parser.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

}  // namespace

bool HttpParser::has_complete_request(const char* data, long len) {
    if (data == nullptr || len <= 0) {
        return false;
    }
    std::string raw(data, static_cast<size_t>(len));
    size_t head_end = raw.find("\r\n\r\n");
    if (head_end == std::string::npos) {
        return false;
    }

    size_t content_length = 0;
    std::istringstream iss(raw.substr(0, head_end));
    std::string line;
    std::getline(iss, line);
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (key == "Content-Length") {
            content_length = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
    }

    const size_t total_need = head_end + 4 + content_length;
    return raw.size() >= total_need;
}

bool HttpParser::parse(const char* data, long len, HttpRequest& out_req) {
    if (data == nullptr || len <= 0) {
        return false;
    }
    std::string raw(data, static_cast<size_t>(len));
    size_t head_end = raw.find("\r\n\r\n");
    if (head_end == std::string::npos) {
        return false;
    }

    std::istringstream headers_stream(raw.substr(0, head_end));
    std::string req_line;
    if (!std::getline(headers_stream, req_line)) {
        return false;
    }
    if (!req_line.empty() && req_line.back() == '\r') {
        req_line.pop_back();
    }
    {
        std::istringstream rl(req_line);
        if (!(rl >> out_req.method >> out_req.path >> out_req.version)) {
            return false;
        }
    }

    out_req.headers.clear();
    std::string line;
    while (std::getline(headers_stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        out_req.headers[key] = value;
    }

    out_req.body.clear();
    if (head_end + 4 < raw.size()) {
        out_req.body = raw.substr(head_end + 4);
    }
    return true;
}

std::string HttpParser::get_header(const HttpRequest& req, const std::string& key) {
    auto it = req.headers.find(key);
    if (it == req.headers.end()) {
        return "";
    }
    return it->second;
}
