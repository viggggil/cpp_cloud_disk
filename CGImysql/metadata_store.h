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

struct FolderMeta {
    std::string owner;
    std::string path;
    std::int64_t updated_at = 0;
};

// Current hard limits (can be enforced by upper-layer services too):
// - Single file max size: 20 MB
// - Per-user total quota: 200 MB
constexpr std::uint64_t kMaxFileBytes = 20ull * 1024ull * 1024ull;      // 20MB
constexpr std::uint64_t kUserQuotaBytes = 200ull * 1024ull * 1024ull;  // 200MB

class MetadataStore {
public:
    static MetadataStore& instance();

    // Create tables if they do not exist.
    bool init_schema();

    // User management. Password is expected to be a hash at call site.
    bool register_user(const std::string& username, const std::string& password_hash);
    bool verify_user(const std::string& username, const std::string& password_hash);

    // File metadata operations.
    bool upsert_file_meta(const FileMeta& meta);
    bool remove_file_meta(const std::string& owner, const std::string& path);
    bool rename_file_meta(const std::string& owner, const std::string& old_path, const std::string& new_path);
    bool rename_path_prefix(const std::string& owner, const std::string& old_prefix, const std::string& new_prefix);
    std::vector<FileMeta> list_file_meta(const std::string& owner, const std::string& dir, int page, int page_size, int& total);
    bool upsert_folder_meta(const FolderMeta& meta);
    bool remove_folder_meta(const std::string& owner, const std::string& path);
    bool rename_folder_meta(const std::string& owner, const std::string& old_path, const std::string& new_path);
    std::vector<FolderMeta> list_folder_meta(const std::string& owner);

    // Compute current total used bytes for a user from DB.
    std::uint64_t user_used_bytes(const std::string& owner);

private:
    MetadataStore() = default;
    std::string file_key(const std::string& owner, const std::string& path) const;

    std::mutex mu_;
};

#endif
