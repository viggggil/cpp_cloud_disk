#include "auth_manager.h"
#include "../CGImysql/metadata_store.h"
#include "../log/log.h"

#include <crypt.h>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <random>
#include <sstream>

namespace {

constexpr unsigned long kBcryptCost = 12;
constexpr int kBcryptSaltBytes = 16;

std::string bcrypt_hash_password(const std::string& password) {
    if (password.empty()) {
        return std::string();
    }

    std::random_device rd;
    char random_bytes[kBcryptSaltBytes];
    for (int i = 0; i < kBcryptSaltBytes; ++i) {
        random_bytes[i] = static_cast<char>(rd());
    }

    char salt[CRYPT_GENSALT_OUTPUT_SIZE] = {0};
    if (crypt_gensalt_rn("$2b$", kBcryptCost, random_bytes, sizeof(random_bytes), salt, sizeof(salt)) == nullptr) {
        return std::string();
    }

    struct crypt_data data;
    std::memset(&data, 0, sizeof(data));
    char* hashed = crypt_r(password.c_str(), salt, &data);
    if (hashed == nullptr || hashed[0] == '*') {
        return std::string();
    }
    return std::string(hashed);
}

bool bcrypt_verify_password(const std::string& password, const std::string& stored_hash) {
    if (password.empty() || stored_hash.empty()) {
        return false;
    }

    struct crypt_data data;
    std::memset(&data, 0, sizeof(data));
    char* computed = crypt_r(password.c_str(), stored_hash.c_str(), &data);
    if (computed == nullptr || computed[0] == '*') {
        return false;
    }
    return stored_hash == computed;
}

}  // namespace

AuthManager& AuthManager::instance() {
    static AuthManager manager;
    return manager;
}

bool AuthManager::register_user(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {
        Log::get_instance()->write_warn("auth.register reject: empty username or password");
        return false;
    }

    const std::string password_hash = bcrypt_hash_password(password);
    if (password_hash.empty()) {
        Log::get_instance()->write_error("auth.register failed: username=" + username + " reason=bcrypt_hash_failed");
        return false;
    }

    const bool ok = MetadataStore::instance().register_user(username, password_hash);
    if (!ok) {
        Log::get_instance()->write_warn("auth.register failed: username=" + username + " reason=db_insert_failed_or_conflict");
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    users_[username] = password_hash;
    Log::get_instance()->write_info("auth.register success: username=" + username);
    return true;
}

bool AuthManager::verify_user(const std::string& username, const std::string& password) {
    if (MetadataStore::instance().verify_user(username, password)) {
        Log::get_instance()->write_info("auth.login verify success(db): username=" + username);
        return true;
    }
    std::lock_guard<std::mutex> lock(mu_);
    auto it = users_.find(username);
    const bool ok = it != users_.end() && bcrypt_verify_password(password, it->second);
    if (ok) {
        Log::get_instance()->write_warn("auth.login verify success(memory-fallback): username=" + username);
    } else {
        Log::get_instance()->write_warn("auth.login verify failed: username=" + username);
    }
    return ok;
}

std::string AuthManager::random_token(const std::string& prefix, const std::string& username) {
    std::ostringstream oss;
    oss << prefix << "_" << username << "_" << std::time(nullptr) << "_" << (std::rand() % 1000000);
    return oss.str();
}

std::string AuthManager::issue_session(const std::string& username, int ttl_seconds) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string sid = random_token("sid", username);
    SessionInfo info;
    info.username = username;
    info.expires_at = static_cast<std::int64_t>(std::time(nullptr)) + ttl_seconds;
    session_to_user_[sid] = info;
    Log::get_instance()->write_info("auth.session issued: username=" + username + " ttl_seconds=" + std::to_string(ttl_seconds));
    return sid;
}

bool AuthManager::verify_session(const std::string& sid, std::string& username_out) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = session_to_user_.find(sid);
    if (it == session_to_user_.end()) {
        Log::get_instance()->write_warn("auth.session verify failed: sid_not_found");
        return false;
    }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    if (it->second.expires_at > 0 && it->second.expires_at < now) {
        // Expired; erase and fail.
        Log::get_instance()->write_info("auth.session expired: username=" + it->second.username);
        session_to_user_.erase(it);
        return false;
    }
    username_out = it->second.username;
    return true;
}

bool AuthManager::revoke_session(const std::string& sid, std::string* username_out) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = session_to_user_.find(sid);
    if (it == session_to_user_.end()) {
        Log::get_instance()->write_warn("auth.logout failed: sid_not_found");
        return false;
    }
    if (username_out) {
        *username_out = it->second.username;
    }
    Log::get_instance()->write_info("auth.logout success: username=" + it->second.username);
    session_to_user_.erase(it);
    return true;
}
