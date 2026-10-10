#include "User.h"

User::User(
    const std::string &username, 
    const std::string &passwordHash, 
    const std::string &nickname, 
    const std::string &avatar)
    :m_username(username),
    m_passwordHash(passwordHash),
    m_nickname(nickname),
    m_avatar(avatar)
{
}

const std::string& User::getUsername()
{
    return m_username;
}

const std::string& User::getPasswordHash()
{
    return m_passwordHash;
}

const std::string& User::getNickname()
{
    return m_nickname;
}

const std::string& User::getAvatar()
{
    return m_avatar;
}
