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
    std::string issue_jwt(const std::string& username);
    std::string issue_session(const std::string& username);
    bool verify_jwt(const std::string& token, std::string& username_out);
    bool verify_session(const std::string& sid, std::string& username_out);

private:
    AuthManager() = default;
    std::string random_token(const std::string& prefix, const std::string& username);

    std::unordered_map<std::string, std::string> jwt_to_user_;
    std::unordered_map<std::string, std::string> session_to_user_;
    std::unordered_map<std::string, std::string> users_;
    std::mutex mu_;
};

#endif
