#include "json_utils.h"

#include <cctype>
#include <cstdlib>

namespace {

void skip_spaces(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
}

bool parse_quoted(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '\\' && i < s.size()) {
            out.push_back(s[i++]);
            continue;
        }
        if (c == '"') {
            return true;
        }
        out.push_back(c);
    }
    return false;
}

bool parse_value(const std::string& s, size_t& i, std::string& out) {
    skip_spaces(s, i);
    if (i >= s.size()) {
        return false;
    }
    if (s[i] == '"') {
        return parse_quoted(s, i, out);
    }

    size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}') {
        ++i;
    }
    out = s.substr(start, i - start);
    size_t l = 0;
    while (l < out.size() && std::isspace(static_cast<unsigned char>(out[l]))) {
        ++l;
    }
    size_t r = out.size();
    while (r > l && std::isspace(static_cast<unsigned char>(out[r - 1]))) {
        --r;
    }
    out = out.substr(l, r - l);
    return true;
}

}  // namespace

bool JsonUtils::parse_flat_object(const std::string& json, std::unordered_map<std::string, std::string>& out) {
    out.clear();
    size_t i = 0;
    skip_spaces(json, i);
    if (i >= json.size() || json[i] != '{') {
        return false;
    }
    ++i;

    while (i < json.size()) {
        skip_spaces(json, i);
        if (i < json.size() && json[i] == '}') {
            ++i;
            return true;
        }

        std::string key;
        if (!parse_quoted(json, i, key)) {
            return false;
        }
        skip_spaces(json, i);
        if (i >= json.size() || json[i] != ':') {
            return false;
        }
        ++i;

        std::string value;
        if (!parse_value(json, i, value)) {
            return false;
        }
        out[key] = value;

        skip_spaces(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        if (i < json.size() && json[i] == '}') {
            ++i;
            return true;
        }
        return false;
    }
    return false;
}

std::string JsonUtils::get(const std::unordered_map<std::string, std::string>& obj, const std::string& key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return "";
    }
    return it->second;
}

int JsonUtils::get_int(const std::unordered_map<std::string, std::string>& obj, const std::string& key, int default_val) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return default_val;
    }
    char* end = nullptr;
    long v = std::strtol(it->second.c_str(), &end, 10);
    if (end == it->second.c_str()) {
        return default_val;
    }
    return static_cast<int>(v);
}
