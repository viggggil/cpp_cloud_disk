#ifndef METADATA_STORE_H
#define METADATA_STORE_H

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct FileMeta {
    std::string owner;
    std::string path;
    std::uint64_t size = 0;
    std::int64_t updated_at = 0;
};

class MetadataStore {
public:
    static MetadataStore& instance();

    bool init_schema();

    bool register_user(const std::string& username, const std::string& password_hash);
    bool verify_user(const std::string& username, const std::string& password_hash);

    void upsert_file_meta(const FileMeta& meta);
    bool remove_file_meta(const std::string& owner, const std::string& path);
    bool rename_file_meta(const std::string& owner, const std::string& old_path, const std::string& new_path);
    std::vector<FileMeta> list_file_meta(const std::string& owner, const std::string& dir, int page, int page_size, int& total);

private:
    MetadataStore() = default;
    std::string file_key(const std::string& owner, const std::string& path) const;

    std::mutex mu_;
    std::unordered_map<std::string, std::string> users_;
    std::unordered_map<std::string, FileMeta> file_meta_;
};

#endif
