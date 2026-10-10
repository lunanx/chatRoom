#include "UserRepository.h"

bool UserRepository::findByUsername(const std::string &username)
{
    DatabaseManager DBManager;
    DBManager.Connection();
    return DBManager.exisit(
        "SELECT 1 FROM User WHERE username = ? LIMIT 1", username);
}

bool UserRepository::createUser(const User &user)
{
    DatabaseManager DBManager;
    DBManager.Connection();
    std::int32_t res = DBManager.executeUpdate(
        "INSERT INTO User (username, password_hash, nickname, avatar) VALUES (?, ?, ?, ?)",
        const_cast<User &>(user).getUsername(),
        const_cast<User &>(user).getPasswordHash(),
        const_cast<User &>(user).getNickname(),
        const_cast<User &>(user).getAvatar());
    if (res == 0)
    {
        std::cout << "已存在,插入失败\n";
        return false;
    }
    else if (res == -1)
    {
        return false;
    }
    else
    {
        return true;
    }
}

std::unique_ptr<sql::ResultSet> UserRepository::getPasswordHashByUsername(const std::string &username)
{
    DatabaseManager DBManager;
    DBManager.Connection();
    return DBManager.query(
        "SELECT password_hash FROM User WHERE username = ? LIMIT 1", username);
}

bool UserRepository::updateOnlineStatusByUsername(const std::string &username,int status)
{
    DatabaseManager DBManager;
    DBManager.Connection();
    return DBManager.executeUpdate(
        "UPDATE User SET online = ? WHERE username = ?",status,username
    ) > 0;
}
