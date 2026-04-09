#include "file_service.h"
#include "../CGImysql/metadata_store.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
const char* kStorageRoot = "./root/storage";
constexpr std::size_t kMaxTextPreviewBytes = 1024 * 1024;

// Very small whitelist of allowed file extensions.
// You can extend this as needed.
bool allow_extension(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos || pos == path.size() - 1) return false;
    std::string ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "txt" || ext == "md" || ext == "pdf" || ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "zip";
}

std::string lower_ext(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos || pos == path.size() - 1) return "";
    std::string ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool is_text_extension(const std::string& path) {
    const std::string ext = lower_ext(path);
    return ext == "txt" || ext == "md";
}

std::string path_basename(const std::string& path) {
    fs::path p(path);
    return p.filename().string();
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

bool FileService::validate_rel_path(const std::string& rel, std::string& err) {
    if (rel.empty()) {
        err = "invalid path";
        return false;
    }
    if (rel.size() > 255) {
        err = "path too long (max 255)";
        return false;
    }
    if (!allow_extension(rel)) {
        err = "unsupported file type";
        return false;
    }
    return true;
}

bool FileService::persist_file(const std::string& user, const std::string& rel_path, const unsigned char* data, std::uint64_t size, std::string& err) {
    std::string full_path;
    if (!safe_user_path(user, rel_path, full_path)) {
        err = "invalid path";
        return false;
    }
    if (!validate_rel_path(rel_path, err)) {
        return false;
    }
    if (size > kMaxFileBytes) {
        err = "file too large (max 20MB)";
        return false;
    }

    std::uint64_t old_size = 0;
    int total = 0;
    const auto metas = MetadataStore::instance().list_file_meta(user, "", 1, 100000, total);
    for (const auto& m : metas) {
        if (m.path == rel_path) {
            old_size = m.size;
            break;
        }
    }

    const std::uint64_t used = MetadataStore::instance().user_used_bytes(user);
    if (used - old_size + size > kUserQuotaBytes) {
        err = "user quota exceeded (200MB)";
        return false;
    }

    fs::create_directories(fs::path(full_path).parent_path());
    std::ofstream ofs(full_path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        err = "cannot open file";
        return false;
    }
    if (size > 0 && data != nullptr) {
        ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
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

bool FileService::upload_text_file(const std::string& user, const std::string& rel_path, const std::string& content, std::string& err) {
    return persist_file(user,
                        rel_path,
                        reinterpret_cast<const unsigned char*>(content.data()),
                        static_cast<std::uint64_t>(content.size()),
                        err);
}

bool FileService::download_text_file(const std::string& user, const std::string& rel_path, std::string& content, std::string& err) {
    if (!is_text_extension(rel_path)) {
        err = "preview only supports text files";
        return false;
    }
    std::vector<unsigned char> data;
    if (!read_file_binary(user, rel_path, data, err, nullptr)) {
        return false;
    }
    if (data.size() > kMaxTextPreviewBytes) {
        err = "text preview too large";
        return false;
    }
    content.assign(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

bool FileService::upload_binary_file(const std::string& user, const std::string& rel_path, const std::vector<unsigned char>& data, std::string& err) {
    return persist_file(user, rel_path, data.data(), static_cast<std::uint64_t>(data.size()), err);
}

bool FileService::read_file_binary(const std::string& user,
                                   const std::string& rel_path,
                                   std::vector<unsigned char>& data,
                                   std::string& err,
                                   std::string* full_path_out) {
    std::string full_path;
    if (!safe_user_path(user, rel_path, full_path)) {
        err = "invalid path";
        return false;
    }
    if (!validate_rel_path(rel_path, err)) {
        return false;
    }
    std::ifstream ifs(full_path, std::ios::binary);
    if (!ifs.is_open()) {
        err = "file not found";
        return false;
    }
    data.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    if (!ifs.good() && !ifs.eof()) {
        err = "read failed";
        return false;
    }
    if (full_path_out) {
        *full_path_out = full_path;
    }
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
        const std::string content_type = guess_content_type(m.path);
        const bool previewable = is_text_extension(m.path);
        oss << "{\"path\":\"" << escape_json(m.path)
            << "\",\"name\":\"" << escape_json(path_basename(m.path))
            << "\",\"size\":" << m.size
            << ",\"updated_at\":" << m.updated_at
            << ",\"content_type\":\"" << escape_json(content_type)
            << "\",\"previewable\":" << (previewable ? "true" : "false")
            << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string FileService::guess_content_type(const std::string& rel_path) {
    const std::string ext = lower_ext(rel_path);
    if (ext == "txt") return "text/plain; charset=utf-8";
    if (ext == "md") return "text/markdown; charset=utf-8";
    if (ext == "pdf") return "application/pdf";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "zip") return "application/zip";
    return "application/octet-stream";
}
