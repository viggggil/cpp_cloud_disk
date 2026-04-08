#include "auth_manager.h"
#include "../CGImysql/metadata_store.h"

#include <cstdlib>
#include <ctime>
#include <sstream>

AuthManager& AuthManager::instance() {
    static AuthManager manager;
    return manager;
}

bool AuthManager::register_user(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {
        return false;
    }
    // TODO: replace plain password with salted hash before persistent storage.
    // For now we store the raw password string as a "hash" for demo purposes.
    const bool ok = MetadataStore::instance().register_user(username, password);
    if (!ok) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    users_[username] = password;
    return true;
}

bool AuthManager::verify_user(const std::string& username, const std::string& password) {
    if (MetadataStore::instance().verify_user(username, password)) {
        return true;
    }
    std::lock_guard<std::mutex> lock(mu_);
    auto it = users_.find(username);
    return it != users_.end() && it->second == password;
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
    return sid;
}

bool AuthManager::verify_session(const std::string& sid, std::string& username_out) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = session_to_user_.find(sid);
    if (it == session_to_user_.end()) {
        return false;
    }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    if (it->second.expires_at > 0 && it->second.expires_at < now) {
        // Expired; erase and fail.
        session_to_user_.erase(it);
        return false;
    }
    username_out = it->second.username;
    return true;
}
