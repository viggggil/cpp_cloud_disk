#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>
#include <unordered_map>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

class HttpParser {
public:
    static bool has_complete_request(const char* data, long len);
    static bool parse(const char* data, long len, HttpRequest& out_req);
    static std::string get_header(const HttpRequest& req, const std::string& key);
};

#endif
