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
        
private:
    std::string m_username;
    std::string m_passwordHash;
    std::string m_nickname;
    std::string m_avatar;
};
