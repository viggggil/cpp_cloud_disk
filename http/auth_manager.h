#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <string>
#include <mutex>
#include <unordered_map>

class AuthManager {
public:
    static AuthManager& instance();

    bool register_user(const std::string& username, const std::string& password);
    bool verify_user(const std::string& username, const std::string& password);
    // Issue a new session id that is valid for ttl_seconds from now.
    std::string issue_session(const std::string& username, int ttl_seconds);
    bool verify_session(const std::string& sid, std::string& username_out);
    bool revoke_session(const std::string& sid, std::string* username_out = nullptr);

private:
    AuthManager() = default;
    std::string random_token(const std::string& prefix, const std::string& username);

    struct SessionInfo {
        std::string username;
        std::int64_t expires_at = 0;  // epoch seconds
    };

    std::unordered_map<std::string, SessionInfo> session_to_user_;
    std::unordered_map<std::string, std::string> users_;
    std::mutex mu_;
};

#endif
