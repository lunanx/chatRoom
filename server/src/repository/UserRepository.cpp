#include "UserRepository.h"

bool UserRepository::findByUsername(const std::string &username)
{
    DatabaseManager DBManager;
    if(!DBManager.connect())
    {
        printf("数据库连接失败\n");
        return false;
    }
    return DBManager.exisit(
        "SELECT 1 FROM User WHERE username = ? LIMIT 1", username);
}

bool UserRepository::createUser(const User &user)
{
    DatabaseManager DBManager;
    if(!DBManager.connect())
    {
        printf("数据库连接失败\n");
        return false;
    }
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

std::string UserRepository::getPasswordHashByUsername(const std::string &username)
{
    DatabaseManager DBManager;
    if(!DBManager.connect())
    {
        printf("数据库连接失败\n");
        return "";
    }
    std::unique_ptr<sql::ResultSet> rs = DBManager.query(
        "SELECT password_hash FROM User WHERE username = ? LIMIT 1", username);
    sql::SQLString PWDHash = rs->getString("password_hash");
    std::string passwordHash(PWDHash.c_str(), PWDHash.length());
    return passwordHash;
}

bool UserRepository::updateOnlineStatusByUsername(const std::string &username,int status)
{
    DatabaseManager DBManager;
    if(!DBManager.connect())
    {
        printf("数据库连接失败\n");
        return false;
    }
    return DBManager.executeUpdate(
        "UPDATE User SET online = ? WHERE username = ?",status,username
    ) > 0;
}
