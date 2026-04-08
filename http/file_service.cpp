#include "file_service.h"
#include "../CGImysql/metadata_store.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
const char* kStorageRoot = "./root/storage";

// Very small whitelist of allowed file extensions.
// You can extend this as needed.
bool allow_extension(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos || pos == path.size() - 1) return false;
    std::string ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "txt" || ext == "md" || ext == "pdf" || ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "zip";
}

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
}  // namespace

bool FileService::safe_user_path(const std::string& user, const std::string& rel, std::string& full_path) {
    if (user.empty() || rel.empty()) {
        return false;
    }
    if (rel.find("..") != std::string::npos) {
        return false;
    }

    fs::path root = fs::path(kStorageRoot) / user;
    fs::path target = root / rel;
    full_path = target.lexically_normal().string();
    const std::string normalized_root = root.lexically_normal().string();
    return full_path.rfind(normalized_root, 0) == 0;
}

bool FileService::upload_text_file(const std::string& user, const std::string& rel_path, const std::string& content, std::string& err) {
    std::string full_path;
    if (!safe_user_path(user, rel_path, full_path)) {
        err = "invalid path";
        return false;
    }

    if (!allow_extension(rel_path)) {
        err = "unsupported file type";
        return false;
    }

    const std::uint64_t size = static_cast<std::uint64_t>(content.size());
    if (size > kMaxFileBytes) {
        err = "file too large (max 20MB)";
        return false;
    }

    // Pre-check user quota based on current metadata.
    const std::uint64_t used = MetadataStore::instance().user_used_bytes(user);
    if (used + size > kUserQuotaBytes) {
        err = "user quota exceeded (200MB)";
        return false;
    }

    fs::create_directories(fs::path(full_path).parent_path());
    std::ofstream ofs(full_path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        err = "cannot open file";
        return false;
    }
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!ofs.good()) {
        err = "write failed";
        return false;
    }
    FileMeta meta;
    meta.owner = user;
    meta.path = rel_path;
    meta.size = size;
    if (!MetadataStore::instance().upsert_file_meta(meta)) {
        err = "metadata persist failed or quota exceeded";
        return false;
    }
    return true;
}

bool FileService::download_text_file(const std::string& user, const std::string& rel_path, std::string& content, std::string& err) {
    std::string full_path;
    if (!safe_user_path(user, rel_path, full_path)) {
        err = "invalid path";
        return false;
    }
    std::ifstream ifs(full_path, std::ios::binary);
    if (!ifs.is_open()) {
        err = "file not found";
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    content = oss.str();
    return true;
}

bool FileService::delete_file(const std::string& user, const std::string& rel_path, std::string& err) {
    std::string full_path;
    if (!safe_user_path(user, rel_path, full_path)) {
        err = "invalid path";
        return false;
    }
    if (!fs::exists(full_path)) {
        err = "file not found";
        return false;
    }
    if (!fs::remove(full_path)) {
        err = "delete failed";
        return false;
    }
    MetadataStore::instance().remove_file_meta(user, rel_path);
    return true;
}

bool FileService::rename_file(const std::string& user, const std::string& old_rel_path, const std::string& new_rel_path, std::string& err) {
    std::string old_full;
    std::string new_full;
    if (!safe_user_path(user, old_rel_path, old_full) || !safe_user_path(user, new_rel_path, new_full)) {
        err = "invalid path";
        return false;
    }
    fs::create_directories(fs::path(new_full).parent_path());
    std::error_code ec;
    fs::rename(old_full, new_full, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    MetadataStore::instance().rename_file_meta(user, old_rel_path, new_rel_path);
    return true;
}

std::string FileService::list_files_json(const std::string& user, const std::string& rel_dir, int page, int page_size) {
    std::string full_path;
    if (!safe_user_path(user, rel_dir.empty() ? "." : rel_dir, full_path)) {
        return "{\"ok\":false,\"error\":\"invalid path\"}";
    }

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 20;

    int total = 0;
    const auto metas = MetadataStore::instance().list_file_meta(user, rel_dir, page, page_size, total);

    std::ostringstream oss;
    oss << "{\"ok\":true,\"total\":" << total << ",\"page\":" << page << ",\"page_size\":" << page_size << ",\"items\":[";
    bool first = true;
    for (const auto& m : metas) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"path\":\"" << escape_json(m.path) << "\",\"size\":" << m.size << ",\"updated_at\":" << m.updated_at << "}";
    }
    oss << "]}";
    return oss.str();
}
