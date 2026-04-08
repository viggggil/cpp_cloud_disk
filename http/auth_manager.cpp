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

std::string AuthManager::issue_jwt(const std::string& username) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string token = random_token("jwt", username);
    jwt_to_user_[token] = username;
    return token;
}

std::string AuthManager::issue_session(const std::string& username) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string sid = random_token("sid", username);
    session_to_user_[sid] = username;
    return sid;
}

bool AuthManager::verify_jwt(const std::string& token, std::string& username_out) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = jwt_to_user_.find(token);
    if (it == jwt_to_user_.end()) {
        return false;
    }
    username_out = it->second;
    return true;
}

bool AuthManager::verify_session(const std::string& sid, std::string& username_out) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = session_to_user_.find(sid);
    if (it == session_to_user_.end()) {
        return false;
    }
    username_out = it->second;
    return true;
}
