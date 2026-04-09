#include "auth_manager.h"
#include "../CGImysql/metadata_store.h"
#include "../log/log.h"

#include <cstdlib>
#include <ctime>
#include <sstream>

AuthManager& AuthManager::instance() {
    static AuthManager manager;
    return manager;
}

bool AuthManager::register_user(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {
        Log::get_instance()->write_warn("auth.register reject: empty username or password");
        return false;
    }
    // TODO: replace plain password with salted hash before persistent storage.
    // For now we store the raw password string as a "hash" for demo purposes.
    const bool ok = MetadataStore::instance().register_user(username, password);
    if (!ok) {
        Log::get_instance()->write_warn("auth.register failed: username=" + username + " reason=db_insert_failed_or_conflict");
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    users_[username] = password;
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
    const bool ok = it != users_.end() && it->second == password;
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
