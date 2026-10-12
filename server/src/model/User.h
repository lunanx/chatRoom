#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
class User
{
public:
    User();

    User(
        const std::string &username,
        const std::string &passwordHash,
        const std::string &nickname,
        const std::string &avatar);

    const std::string& getUsername() const;
    const std::string& getPasswordHash() const;
    const std::string& getNickname() const;
    const std::string& getAvatar() const;

        
private:
    std::string m_username;
    std::string m_passwordHash;
    std::string m_nickname;
    std::string m_avatar;
};
