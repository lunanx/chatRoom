#include "UserRepository.h"
#include <iostream>

namespace
{
    // MariaDB/MySQL ER_DUP_ENTRY：违反 UNIQUE/PRIMARY KEY 约束。
    // 消除Magic Number
    // 如 error_code == 1062,不知道这个1062是什么东西
    constexpr int DUPLICATE_ENTRY_ERROR = 1062;
}

UserQueryStatus UserRepository::findByUsername(const std::string &username)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询用户名\n";
        return UserQueryStatus::Error;
    }
    auto queryResult = dbManager.query(
        "SELECT 1 FROM User WHERE username = ? LIMIT 1", username);

    if (!queryResult.succeeded || !queryResult.result)
    {
        return UserQueryStatus::Error;
    }

    try
    {
        return queryResult.result->next() ? UserQueryStatus::Found : UserQueryStatus::NotFound;
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "查询用户名失败: " << e.what()
                  << "，错误代码: " << e.getErrorCode() << '\n';
        return UserQueryStatus::Error;
    }
}

CreateUserResult UserRepository::createUser(const User &user)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法创建用户名\n";
        return CreateUserResult::Failed;
    }

    const int affectedRows = dbManager.executeUpdate(
        "INSERT INTO User (username,password_hash,nickname,avatar) VALUES (?, ?, ?, ?)",
        user.getUsername(),
        user.getPasswordHash(),
        user.getNickname(),
        user.getAvatar());

    if (affectedRows == 1)
    {
        return CreateUserResult::Created;
    }

    if (dbManager.lastErrorCode() == DUPLICATE_ENTRY_ERROR)
    {
        // 预检查后其他连接可能先插入了同名用户名；由数据库唯一约束最终拦截。
        return CreateUserResult::UsernameExists;
    }

    // INSERT 正常情况下应创建一行。0 或其他异常结果均视为失败。
    return CreateUserResult::Failed;
}

LoginResult UserRepository::getUserInfoForLoginByUsername(const std::string &username)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询" << username << "用户的密码哈希\n";
        return {UserQueryStatus::Error, {}, -1, {}};
    }

    // 只允许正常账号登录。用户名不存在和账号不可用都不会返回密码哈希。
    auto queryResult = dbManager.query(
        "SELECT password_hash,id,username FROM User WHERE username = ? AND status = 1 LIMIT 1",
        username);

    if (!queryResult.succeeded || !queryResult.result)
    {
        return {UserQueryStatus::Error, {}, -1, {}};
    }

    try
    {
        if (!queryResult.result->next())
        {
            return {UserQueryStatus::NotFound, {}, -1, {}};
        }
        const sql::SQLString hash = queryResult.result->getString("password_hash");
        std::uint64_t id = queryResult.result->getUInt64("id");
        const sql::SQLString name = queryResult.result->getString("username");
        return {UserQueryStatus::Found, std::string(hash.c_str(), hash.length()),id,std::string(name.c_str(),name.length())};
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "查询用户密码哈希失败: " << e.what()
                  << "，错误代码: " << e.getErrorCode() << '\n';
        return {UserQueryStatus::Error, {}, -1, {}};
    }
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

    // UPDATE 返回 0 也可能代表字段原本就是目标值；只有 -1 表示本层检测到 SQL 错误。
    return dbManager.executeUpdate(
               "UPDATE User SET online = ? WHERE username = ?", status, username) >= 0;
}
