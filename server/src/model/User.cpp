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

const std::string& User::getUsername() const
{
    return m_username;
}

const std::string& User::getPasswordHash() const
{
    return m_passwordHash;
}

const std::string& User::getNickname() const
{
    return m_nickname;
}

const std::string& User::getAvatar() const
{
    return m_avatar;
}
