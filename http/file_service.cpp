#include "file_service.h"
#include "../CGImysql/metadata_store.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
const char* kStorageRoot = "./root/storage";
constexpr std::size_t kMaxTextPreviewBytes = 1024 * 1024;

std::string trim_slashes(const std::string& path) {
    size_t start = 0;
    while (start < path.size() && path[start] == '/') ++start;
    size_t end = path.size();
    while (end > start && path[end - 1] == '/') --end;
    return path.substr(start, end - start);
}

std::string parent_dir_of(const std::string& path) {
    const std::string norm = trim_slashes(path);
    const size_t pos = norm.find_last_of('/');
    if (pos == std::string::npos) return "";
    return norm.substr(0, pos);
}

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

bool is_image_extension(const std::string& path) {
    const std::string ext = lower_ext(path);
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif";
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
    const std::string norm = trim_slashes(rel);
    if (norm.empty()) {
        err = "invalid path";
        return false;
    }
    if (norm.size() > 255) {
        err = "path too long (max 255)";
        return false;
    }
    if (!allow_extension(norm)) {
        err = "unsupported file type";
        return false;
    }
    return true;
}

bool FileService::validate_folder_path(const std::string& rel, std::string& err) {
    const std::string norm = trim_slashes(rel);
    if (norm.empty()) {
        err = "invalid folder path";
        return false;
    }
    if (norm.size() > 255) {
        err = "path too long (max 255)";
        return false;
    }
    if (norm.find("..") != std::string::npos) {
        err = "invalid folder path";
        return false;
    }
    return true;
}

bool FileService::persist_file(const std::string& user, const std::string& rel_path, const unsigned char* data, std::uint64_t size, std::string& err) {
    const std::string norm_rel = trim_slashes(rel_path);
    std::string full_path;
    if (!safe_user_path(user, norm_rel, full_path)) {
        err = "invalid path";
        return false;
    }
    if (!validate_rel_path(norm_rel, err)) {
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
        if (m.path == norm_rel) {
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
    meta.path = norm_rel;
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
    const std::string norm_rel = trim_slashes(rel_path);
    if (!safe_user_path(user, norm_rel, full_path)) {
        err = "invalid path";
        return false;
    }
    if (!validate_rel_path(norm_rel, err)) {
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
    const std::string norm_rel = trim_slashes(rel_path);
    std::string full_path;
    if (!safe_user_path(user, norm_rel, full_path)) {
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
    MetadataStore::instance().remove_file_meta(user, norm_rel);
    return true;
}

bool FileService::rename_file(const std::string& user, const std::string& old_rel_path, const std::string& new_rel_path, std::string& err) {
    return rename_path(user, old_rel_path, new_rel_path, err);
}

bool FileService::create_folder(const std::string& user, const std::string& rel_path, std::string& err) {
    const std::string norm_rel = trim_slashes(rel_path);
    std::string full_path;
    if (!safe_user_path(user, norm_rel, full_path)) {
        err = "invalid path";
        return false;
    }
    if (!validate_folder_path(norm_rel, err)) {
        return false;
    }
    std::error_code ec;
    if (fs::exists(full_path)) {
        err = "path already exists";
        return false;
    }
    fs::create_directories(full_path, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    return true;
}

bool FileService::rename_path(const std::string& user, const std::string& old_rel_path, const std::string& new_rel_path, std::string& err) {
    const std::string old_norm = trim_slashes(old_rel_path);
    const std::string new_norm = trim_slashes(new_rel_path);
    std::string old_full;
    std::string new_full;
    if (!safe_user_path(user, old_norm, old_full) || !safe_user_path(user, new_norm, new_full)) {
        err = "invalid path";
        return false;
    }
    const bool is_dir = fs::is_directory(old_full);
    if (is_dir) {
        if (!validate_folder_path(new_norm, err)) return false;
    } else {
        if (!validate_rel_path(new_norm, err)) return false;
    }
    if (!fs::exists(old_full)) {
        err = "path not found";
        return false;
    }
    if (fs::exists(new_full)) {
        err = "target already exists";
        return false;
    }
    fs::create_directories(fs::path(new_full).parent_path());
    std::error_code ec;
    fs::rename(old_full, new_full, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    if (is_dir) {
        int total = 0;
        const auto metas = MetadataStore::instance().list_file_meta(user, "", 1, 100000, total);
        const std::string old_prefix = old_norm + "/";
        const std::string new_prefix = new_norm + "/";
        for (const auto& m : metas) {
            if (m.path == old_norm || m.path.rfind(old_prefix, 0) == 0) {
                std::string target = (m.path == old_norm) ? new_norm : (new_prefix + m.path.substr(old_prefix.size()));
                MetadataStore::instance().rename_file_meta(user, m.path, target);
            }
        }
    } else {
        MetadataStore::instance().rename_file_meta(user, old_norm, new_norm);
    }
    return true;
}

bool FileService::move_path(const std::string& user, const std::string& src_rel_path, const std::string& target_dir, std::string& err) {
    const std::string src_norm = trim_slashes(src_rel_path);
    const std::string dst_dir = trim_slashes(target_dir);
    const fs::path src_path(src_norm);
    const std::string name = src_path.filename().string();
    const std::string new_path = dst_dir.empty() ? name : (dst_dir + "/" + name);
    return rename_path(user, src_norm, new_path, err);
}

std::string FileService::list_files_json(const std::string& user, const std::string& rel_dir, int page, int page_size) {
    const std::string dir_norm = trim_slashes(rel_dir);
    const std::string safe_dir = dir_norm.empty() ? "." : dir_norm;
    std::string full_path;
    if (!safe_user_path(user, safe_dir, full_path)) {
        return "{\"ok\":false,\"error\":\"invalid path\"}";
    }

    fs::path current_dir = dir_norm.empty() ? (fs::path(kStorageRoot) / user) : fs::path(full_path);
    std::error_code ec;
    if (!fs::exists(current_dir, ec)) {
        fs::create_directories(current_dir, ec);
    }
    if (ec) {
        return "{\"ok\":false,\"error\":\"cannot open dir\"}";
    }

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 20;

    struct Entry {
        bool is_dir = false;
        std::string path;
        std::string name;
        std::uint64_t size = 0;
        std::int64_t updated_at = 0;
    };
    std::vector<Entry> entries;
    for (const auto& it : fs::directory_iterator(current_dir, ec)) {
        if (ec) break;
        Entry e;
        e.is_dir = it.is_directory();
        e.name = it.path().filename().string();
        e.path = dir_norm.empty() ? e.name : (dir_norm + "/" + e.name);
        if (!e.is_dir) e.size = static_cast<std::uint64_t>(it.file_size(ec));
        auto ft = it.last_write_time(ec);
        if (!ec) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            e.updated_at = static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(sctp));
        }
        entries.push_back(std::move(e));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
    });
    const int total = static_cast<int>(entries.size());
    const int offset = (page - 1) * page_size;
    const int end = std::min(offset + page_size, total);

    std::ostringstream oss;
    oss << "{\"ok\":true,\"total\":" << total << ",\"page\":" << page << ",\"page_size\":" << page_size << ",\"items\":[";
    bool first = true;
    for (int i = offset; i < end; ++i) {
        const auto& m = entries[i];
        if (!first) oss << ",";
        first = false;
        const std::string content_type = m.is_dir ? "inode/directory" : guess_content_type(m.path);
        const std::string preview_kind = m.is_dir ? "" : guess_preview_kind(m.path);
        const bool previewable = !preview_kind.empty();
        oss << "{\"path\":\"" << escape_json(m.path)
            << "\",\"name\":\"" << escape_json(path_basename(m.path))
            << "\",\"is_dir\":" << (m.is_dir ? "true" : "false")
            << ",\"size\":" << m.size
            << ",\"updated_at\":" << m.updated_at
            << ",\"content_type\":\"" << escape_json(content_type)
            << "\",\"preview_kind\":\"" << escape_json(preview_kind)
            << "\",\"previewable\":" << (previewable ? "true" : "false")
            << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string FileService::directory_tree_json(const std::string& user) {
    fs::path root = fs::path(kStorageRoot) / user;
    std::error_code ec;
    fs::create_directories(root, ec);
    std::vector<std::string> dirs;
    dirs.push_back("");
    for (const auto& it : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!it.is_directory()) continue;
        std::string rel = fs::relative(it.path(), root, ec).string();
        if (!ec) dirs.push_back(trim_slashes(rel));
    }
    std::sort(dirs.begin(), dirs.end());
    std::ostringstream oss;
    oss << "{\"ok\":true,\"items\":[";
    for (size_t i = 0; i < dirs.size(); ++i) {
        if (i) oss << ",";
        oss << "{\"path\":\"" << escape_json(dirs[i]) << "\",\"name\":\""
            << escape_json(dirs[i].empty() ? "/" : path_basename(dirs[i])) << "\"}";
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

std::string FileService::guess_preview_kind(const std::string& rel_path) {
    if (is_text_extension(rel_path)) return "text";
    if (is_image_extension(rel_path)) return "image";
    return "";
}
