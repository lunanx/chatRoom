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

