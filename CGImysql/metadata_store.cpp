#include "metadata_store.h"

#include <algorithm>
#include <ctime>

MetadataStore& MetadataStore::instance() {
    static MetadataStore store;
    return store;
}

bool MetadataStore::init_schema() {
    // Placeholder for real MySQL DDL:
    // CREATE TABLE users(username VARCHAR(64) PRIMARY KEY, password_hash VARCHAR(255) NOT NULL, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);
    // CREATE TABLE file_meta(id BIGINT AUTO_INCREMENT PRIMARY KEY, owner VARCHAR(64) NOT NULL, path VARCHAR(1024) NOT NULL, size BIGINT NOT NULL, updated_at BIGINT NOT NULL, UNIQUE KEY uniq_owner_path(owner, path));
    return true;
}

std::string MetadataStore::file_key(const std::string& owner, const std::string& path) const {
    return owner + ":" + path;
}

bool MetadataStore::register_user(const std::string& username, const std::string& password_hash) {
    std::lock_guard<std::mutex> lock(mu_);
    if (username.empty() || password_hash.empty()) {
        return false;
    }
    if (users_.find(username) != users_.end()) {
        return false;
    }
    users_[username] = password_hash;
    return true;
}

bool MetadataStore::verify_user(const std::string& username, const std::string& password_hash) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = users_.find(username);
    if (it == users_.end()) {
        return false;
    }
    return it->second == password_hash;
}

void MetadataStore::upsert_file_meta(const FileMeta& meta) {
    std::lock_guard<std::mutex> lock(mu_);
    FileMeta copy = meta;
    if (copy.updated_at == 0) {
        copy.updated_at = static_cast<std::int64_t>(std::time(nullptr));
    }
    file_meta_[file_key(copy.owner, copy.path)] = copy;
}

bool MetadataStore::remove_file_meta(const std::string& owner, const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);
    return file_meta_.erase(file_key(owner, path)) > 0;
}

bool MetadataStore::rename_file_meta(const std::string& owner, const std::string& old_path, const std::string& new_path) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string old_key = file_key(owner, old_path);
    auto it = file_meta_.find(old_key);
    if (it == file_meta_.end()) {
        return false;
    }
    FileMeta meta = it->second;
    file_meta_.erase(it);
    meta.path = new_path;
    meta.updated_at = static_cast<std::int64_t>(std::time(nullptr));
    file_meta_[file_key(owner, new_path)] = meta;
    return true;
}

std::vector<FileMeta> MetadataStore::list_file_meta(const std::string& owner, const std::string& dir, int page, int page_size, int& total) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<FileMeta> all;
    std::string prefix = dir;
    if (!prefix.empty() && prefix != "." && prefix.back() != '/') {
        prefix.push_back('/');
    }

    for (const auto& kv : file_meta_) {
        const FileMeta& m = kv.second;
        if (m.owner != owner) {
            continue;
        }
        if (prefix.empty() || prefix == "./" || m.path.rfind(prefix, 0) == 0) {
            all.push_back(m);
        }
    }
    std::sort(all.begin(), all.end(), [](const FileMeta& a, const FileMeta& b) { return a.path < b.path; });
    total = static_cast<int>(all.size());

    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 20;
    }
    const int begin = (page - 1) * page_size;
    const int end = std::min(total, begin + page_size);
    if (begin >= total) {
        return {};
    }
    return std::vector<FileMeta>(all.begin() + begin, all.begin() + end);
}
