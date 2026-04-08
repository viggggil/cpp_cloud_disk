#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include <string>

class FileService {
public:
    static bool upload_text_file(const std::string& user, const std::string& rel_path, const std::string& content, std::string& err);
    static bool download_text_file(const std::string& user, const std::string& rel_path, std::string& content, std::string& err);
    static bool delete_file(const std::string& user, const std::string& rel_path, std::string& err);
    static bool rename_file(const std::string& user, const std::string& old_rel_path, const std::string& new_rel_path, std::string& err);
    static std::string list_files_json(const std::string& user, const std::string& rel_dir, int page, int page_size);

private:
    static bool safe_user_path(const std::string& user, const std::string& rel, std::string& full_path);
};

#endif
