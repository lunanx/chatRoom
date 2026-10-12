#include "UserRepository.h"
#include <iostream>

bool UserRepository::findByUsername(const std::string &username)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询用户名\n";
        return false;
    }
    return dbManager.exisit(
        "SELECT 1 FROM User WHERE username = ? LIMIT 1", username);
}

bool UserRepository::createUser(const User &user)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法创建用户名\n";
        return false;
    }

    const int affectedRows = dbManager.executeUpdate(
        "INSERT INTO User (username,password_hash,nickname,avatar) VALUES (?, ?, ?, ?)",
        user.getUsername(),
        user.getPasswordHash(),
        user.getNickname(),
        user.getAvatar());
    // INSERT 正常成功时应影响一行:重复 username等SQL错误由DatabaseManager记录
    return affectedRows == 1;
}

std::string UserRepository::getPasswordHashByUsername(const std::string &username)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询" << username << "用户的密码哈希\n";
        return {};
    }

    // status = 1是正常账户;禁止已禁用或已注销的账户登录
    auto queryResult = dbManager.query(
        "SELECT password_hash FROM User WHERE username = ? AND status = 1 LIMIT 1",
        username);

    if (!queryResult.result || !queryResult.result->next())
    {
        return {};
    }

    const sql::SQLString hash = queryResult.result->getString("password_hash");
    return std::string(hash.c_str(), hash.length());
}

bool UserRepository::updateOnlineStatusByUsername(const std::string &username, int status)
{
    if (status != 0 && status != 1)
    {
        std::cerr << "在线状态值无效，只允许0或1\n";
        return false;
    }

    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法更新" << username << "用户的在线状态\n";
        return false;
    }

    //状态相同的UPDATE在部分数据库配置下可能返回0，所以只以-1代表SQL错误
    return dbManager.executeUpdate(
        "UPDATE User SET online = ? WHERE username = ?", status, username) >= 0;
}
