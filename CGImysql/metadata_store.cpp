#include "metadata_store.h"

#include <algorithm>
#include <ctime>
#include <cstring>

#include "sql_connection_pool.h"

MetadataStore& MetadataStore::instance() {
    static MetadataStore store;
    return store;
}

bool MetadataStore::init_schema() {
    ConnectionPool* pool = ConnectionPool::get_instance();
    pool->init(8);  // use Config::sql_num via WebServer in future if needed

    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        return false;
    }

    const char* user_sql =
        "CREATE TABLE IF NOT EXISTS users("
        "username VARCHAR(64) PRIMARY KEY,"
        "password_hash VARCHAR(255) NOT NULL,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP" ")";

    if (mysql_query(raw, user_sql) != 0) {
        return false;
    }

    const char* file_sql =
        "CREATE TABLE IF NOT EXISTS file_meta("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "owner VARCHAR(64) NOT NULL,"
        "path VARCHAR(1024) NOT NULL,"
        "size BIGINT NOT NULL,"
        "updated_at BIGINT NOT NULL,"
        "UNIQUE KEY uniq_owner_path(owner, path),"
        "INDEX idx_owner(owner)" ")";

    if (mysql_query(raw, file_sql) != 0) {
        return false;
    }

    return true;
}

std::string MetadataStore::file_key(const std::string& owner, const std::string& path) const {
    return owner + ":" + path;
}

bool MetadataStore::register_user(const std::string& username, const std::string& password_hash) {
    if (username.empty() || password_hash.empty()) {
        return false;
    }

    ConnectionPool* pool = ConnectionPool::get_instance();
    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        return false;
    }

    const char* sql = "INSERT INTO users(username, password_hash) VALUES(?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt) return false;
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2]{};
    unsigned long username_len = static_cast<unsigned long>(username.size());
    unsigned long pwd_len = static_cast<unsigned long>(password_hash.size());

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(username.data());
    bind[0].buffer_length = username_len;
    bind[0].length = &username_len;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(password_hash.data());
    bind[1].buffer_length = pwd_len;
    bind[1].length = &pwd_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

bool MetadataStore::verify_user(const std::string& username, const std::string& password_hash) {
    ConnectionPool* pool = ConnectionPool::get_instance();
    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        return false;
    }

    const char* sql = "SELECT password_hash FROM users WHERE username = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt) return false;
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind_param[1]{};
    unsigned long username_len = static_cast<unsigned long>(username.size());
    bind_param[0].buffer_type = MYSQL_TYPE_STRING;
    bind_param[0].buffer = const_cast<char*>(username.data());
    bind_param[0].buffer_length = username_len;
    bind_param[0].length = &username_len;

    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    char hash_buf[256] = {0};
    unsigned long hash_len = 0;
    MYSQL_BIND bind_res[1]{};
    bind_res[0].buffer_type = MYSQL_TYPE_STRING;
    bind_res[0].buffer = hash_buf;
    bind_res[0].buffer_length = sizeof(hash_buf);
    bind_res[0].length = &hash_len;

    if (mysql_stmt_bind_result(stmt, bind_res) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    bool ok = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        std::string stored(hash_buf, hash_len);
        ok = (stored == password_hash);
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return ok;
}

bool MetadataStore::upsert_file_meta(const FileMeta& meta) {
    if (meta.owner.empty() || meta.path.empty()) {
        return false;
    }
    if (meta.size > kMaxFileBytes) {
        return false;
    }

    ConnectionPool* pool = ConnectionPool::get_instance();
    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        return false;
    }

    // Enforce per-user quota.
    const std::uint64_t used = user_used_bytes(meta.owner);
    if (used + meta.size > kUserQuotaBytes) {
        return false;
    }

    FileMeta copy = meta;
    if (copy.updated_at == 0) {
        copy.updated_at = static_cast<std::int64_t>(std::time(nullptr));
    }

    const char* sql =
        "INSERT INTO file_meta(owner, path, size, updated_at) "
        "VALUES(?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE size=VALUES(size), updated_at=VALUES(updated_at)";

    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt) return false;
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4]{};
    unsigned long owner_len = static_cast<unsigned long>(copy.owner.size());
    unsigned long path_len = static_cast<unsigned long>(copy.path.size());

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(copy.owner.data());
    bind[0].buffer_length = owner_len;
    bind[0].length = &owner_len;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(copy.path.data());
    bind[1].buffer_length = path_len;
    bind[1].length = &path_len;

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = const_cast<std::uint64_t*>(&copy.size);
    bind[2].is_unsigned = true;

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = const_cast<std::int64_t*>(&copy.updated_at);

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

bool MetadataStore::remove_file_meta(const std::string& owner, const std::string& path) {
    ConnectionPool* pool = ConnectionPool::get_instance();
    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        return false;
    }

    const char* sql = "DELETE FROM file_meta WHERE owner = ? AND path = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt) return false;
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2]{};
    unsigned long owner_len = static_cast<unsigned long>(owner.size());
    unsigned long path_len = static_cast<unsigned long>(path.size());

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(owner.data());
    bind[0].buffer_length = owner_len;
    bind[0].length = &owner_len;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(path.data());
    bind[1].buffer_length = path_len;
    bind[1].length = &path_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

bool MetadataStore::rename_file_meta(const std::string& owner, const std::string& old_path, const std::string& new_path) {
    ConnectionPool* pool = ConnectionPool::get_instance();
    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        return false;
    }

    const char* sql = "UPDATE file_meta SET path = ?, updated_at = ? WHERE owner = ? AND path = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt) return false;
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    MYSQL_BIND bind[4]{};
    unsigned long new_path_len = static_cast<unsigned long>(new_path.size());
    unsigned long owner_len = static_cast<unsigned long>(owner.size());
    unsigned long old_path_len = static_cast<unsigned long>(old_path.size());

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(new_path.data());
    bind[0].buffer_length = new_path_len;
    bind[0].length = &new_path_len;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = &now;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char*>(owner.data());
    bind[2].buffer_length = owner_len;
    bind[2].length = &owner_len;

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = const_cast<char*>(old_path.data());
    bind[3].buffer_length = old_path_len;
    bind[3].length = &old_path_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

std::vector<FileMeta> MetadataStore::list_file_meta(const std::string& owner, const std::string& dir, int page, int page_size, int& total) {
    ConnectionPool* pool = ConnectionPool::get_instance();
    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        total = 0;
        return {};
    }

    std::string prefix = dir;
    if (!prefix.empty() && prefix != "." && prefix.back() != '/') {
        prefix.push_back('/');
    }

    // First, count total rows.
    std::string count_sql = "SELECT COUNT(*) FROM file_meta WHERE owner = ?";
    if (!prefix.empty() && prefix != "./") {
        count_sql += " AND path LIKE ?";
    }

    MYSQL_STMT* count_stmt = mysql_stmt_init(raw);
    if (!count_stmt) {
        total = 0;
        return {};
    }
    if (mysql_stmt_prepare(count_stmt, count_sql.c_str(), static_cast<unsigned long>(count_sql.size())) != 0) {
        mysql_stmt_close(count_stmt);
        total = 0;
        return {};
    }

    MYSQL_BIND count_bind[2]{};
    unsigned long owner_len = static_cast<unsigned long>(owner.size());
    count_bind[0].buffer_type = MYSQL_TYPE_STRING;
    count_bind[0].buffer = const_cast<char*>(owner.data());
    count_bind[0].buffer_length = owner_len;
    count_bind[0].length = &owner_len;

    std::string like_pattern;
    unsigned long like_len = 0;
    if (!prefix.empty() && prefix != "./") {
        like_pattern = prefix + "%";
        like_len = static_cast<unsigned long>(like_pattern.size());
        count_bind[1].buffer_type = MYSQL_TYPE_STRING;
        count_bind[1].buffer = const_cast<char*>(like_pattern.data());
        count_bind[1].buffer_length = like_len;
        count_bind[1].length = &like_len;
    }

    if (mysql_stmt_bind_param(count_stmt, count_bind) != 0) {
        mysql_stmt_close(count_stmt);
        total = 0;
        return {};
    }

    if (mysql_stmt_execute(count_stmt) != 0) {
        mysql_stmt_close(count_stmt);
        total = 0;
        return {};
    }

    long long count_val = 0;
    MYSQL_BIND count_res[1]{};
    count_res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    count_res[0].buffer = &count_val;

    if (mysql_stmt_bind_result(count_stmt, count_res) != 0) {
        mysql_stmt_close(count_stmt);
        total = 0;
        return {};
    }

    if (mysql_stmt_store_result(count_stmt) != 0 || mysql_stmt_fetch(count_stmt) != 0) {
        mysql_stmt_close(count_stmt);
        total = 0;
        return {};
    }
    mysql_stmt_free_result(count_stmt);
    mysql_stmt_close(count_stmt);
    total = static_cast<int>(count_val);

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 20;

    const int offset = (page - 1) * page_size;

    // Then, fetch paginated rows.
    std::string list_sql =
        "SELECT owner, path, size, updated_at FROM file_meta WHERE owner = ?";
    if (!prefix.empty() && prefix != "./") {
        list_sql += " AND path LIKE ?";
    }
    list_sql += " ORDER BY path ASC LIMIT ? OFFSET ?";

    MYSQL_STMT* list_stmt = mysql_stmt_init(raw);
    if (!list_stmt) {
        return {};
    }
    if (mysql_stmt_prepare(list_stmt, list_sql.c_str(), static_cast<unsigned long>(list_sql.size())) != 0) {
        mysql_stmt_close(list_stmt);
        return {};
    }

    // Bind parameters: owner, [like], limit, offset
    MYSQL_BIND list_bind[4]{};
    owner_len = static_cast<unsigned long>(owner.size());
    list_bind[0].buffer_type = MYSQL_TYPE_STRING;
    list_bind[0].buffer = const_cast<char*>(owner.data());
    list_bind[0].buffer_length = owner_len;
    list_bind[0].length = &owner_len;

    int idx = 1;
    if (!prefix.empty() && prefix != "./") {
        like_pattern = prefix + "%";
        like_len = static_cast<unsigned long>(like_pattern.size());
        list_bind[idx].buffer_type = MYSQL_TYPE_STRING;
        list_bind[idx].buffer = const_cast<char*>(like_pattern.data());
        list_bind[idx].buffer_length = like_len;
        list_bind[idx].length = &like_len;
        ++idx;
    }

    long long limit_ll = page_size;
    long long offset_ll = offset;
    list_bind[idx].buffer_type = MYSQL_TYPE_LONGLONG;
    list_bind[idx].buffer = &limit_ll;
    ++idx;
    list_bind[idx].buffer_type = MYSQL_TYPE_LONGLONG;
    list_bind[idx].buffer = &offset_ll;

    if (mysql_stmt_bind_param(list_stmt, list_bind) != 0) {
        mysql_stmt_close(list_stmt);
        return {};
    }

    if (mysql_stmt_execute(list_stmt) != 0) {
        mysql_stmt_close(list_stmt);
        return {};
    }

    char owner_buf[65] = {0};
    char path_buf[1025] = {0};
    unsigned long owner_out_len = 0, path_out_len = 0;
    unsigned long long size_val = 0;
    long long updated_at_val = 0;

    MYSQL_BIND res_bind[4]{};
    res_bind[0].buffer_type = MYSQL_TYPE_STRING;
    res_bind[0].buffer = owner_buf;
    res_bind[0].buffer_length = sizeof(owner_buf);
    res_bind[0].length = &owner_out_len;

    res_bind[1].buffer_type = MYSQL_TYPE_STRING;
    res_bind[1].buffer = path_buf;
    res_bind[1].buffer_length = sizeof(path_buf);
    res_bind[1].length = &path_out_len;

    res_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    res_bind[2].buffer = &size_val;
    res_bind[2].is_unsigned = true;

    res_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    res_bind[3].buffer = &updated_at_val;

    if (mysql_stmt_bind_result(list_stmt, res_bind) != 0) {
        mysql_stmt_close(list_stmt);
        return {};
    }

    if (mysql_stmt_store_result(list_stmt) != 0) {
        mysql_stmt_close(list_stmt);
        return {};
    }

    std::vector<FileMeta> result;
    while (true) {
        int fetch_ret = mysql_stmt_fetch(list_stmt);
        if (fetch_ret == MYSQL_NO_DATA || fetch_ret == MYSQL_DATA_TRUNCATED) {
            break;
        }
        if (fetch_ret != 0) {
            break;
        }
        FileMeta m;
        m.owner.assign(owner_buf, owner_out_len);
        m.path.assign(path_buf, path_out_len);
        m.size = static_cast<std::uint64_t>(size_val);
        m.updated_at = static_cast<std::int64_t>(updated_at_val);
        result.push_back(std::move(m));
    }

    mysql_stmt_free_result(list_stmt);
    mysql_stmt_close(list_stmt);
    return result;
}

std::uint64_t MetadataStore::user_used_bytes(const std::string& owner) {
    ConnectionPool* pool = ConnectionPool::get_instance();
    MYSQL* raw = nullptr;
    ConnectionRAII conn(&raw, pool);
    if (!raw) {
        return 0;
    }

    const char* sql = "SELECT IFNULL(SUM(size), 0) FROM file_meta WHERE owner = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(raw);
    if (!stmt) return 0;
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND bind[1]{};
    unsigned long owner_len = static_cast<unsigned long>(owner.size());
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(owner.data());
    bind[0].buffer_length = owner_len;
    bind[0].length = &owner_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    unsigned long long sum_val = 0;
    MYSQL_BIND res_bind[1]{};
    res_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res_bind[0].buffer = &sum_val;
    res_bind[0].is_unsigned = true;

    if (mysql_stmt_bind_result(stmt, res_bind) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }
    if (mysql_stmt_store_result(stmt) != 0 || mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return static_cast<std::uint64_t>(sum_val);
}
