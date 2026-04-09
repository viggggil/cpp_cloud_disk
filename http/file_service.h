#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include <cstdint>
#include <string>
#include <vector>

class FileService {
public:
    static bool upload_text_file(const std::string& user, const std::string& rel_path, const std::string& content, std::string& err);
    static bool download_text_file(const std::string& user, const std::string& rel_path, std::string& content, std::string& err);
    static bool upload_binary_file(const std::string& user, const std::string& rel_path, const std::vector<unsigned char>& data, std::string& err);
    static bool read_file_binary(const std::string& user, const std::string& rel_path, std::vector<unsigned char>& data, std::string& err, std::string* full_path_out = nullptr);
    static bool delete_file(const std::string& user, const std::string& rel_path, std::string& err);
    static bool rename_file(const std::string& user, const std::string& old_rel_path, const std::string& new_rel_path, std::string& err);
    static std::string list_files_json(const std::string& user, const std::string& rel_dir, int page, int page_size);
    static std::string guess_content_type(const std::string& rel_path);

private:
    static bool safe_user_path(const std::string& user, const std::string& rel, std::string& full_path);
    static bool validate_rel_path(const std::string& rel, std::string& err);
    static bool persist_file(const std::string& user, const std::string& rel_path, const unsigned char* data, std::uint64_t size, std::string& err);
};

#endif
