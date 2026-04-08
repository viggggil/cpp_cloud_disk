#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <string>
#include <unordered_map>

class JsonUtils {
public:
    static bool parse_flat_object(const std::string& json, std::unordered_map<std::string, std::string>& out);
    static std::string get(const std::unordered_map<std::string, std::string>& obj, const std::string& key);
    static int get_int(const std::unordered_map<std::string, std::string>& obj, const std::string& key, int default_val);
};

#endif
