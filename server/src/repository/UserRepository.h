#pragma once

#include "db/DatabaseManager.h"
#include "model/User.h"
#include <string>

enum class UserQueryStatus
{
    Found,
    NotFound,
    Error
};

struct PasswordHashQueryResult
{
    UserQueryStatus status{UserQueryStatus::Error};
    std::string passwordHash;
    int userId;
    std::string username;
};

enum class CreateUserResult
{
    Created,
    UsernameExists,
    Failed
};

class UserRepository
{
public:
    /*
        查询用户名是否存在。数据库错误与“不存在”使用不同结果表示。
    */
    static UserQueryStatus findByUsername(const std::string& username);

    /*
        创建用户。并发注册发生唯一键冲突时，返回 UsernameExists。
    */
    static CreateUserResult createUser(const User& user);

    /*
        取得可登录账号的密码哈希。status != 1 的账号视为不可登录。
        成功则会返回userId,username,password_hash,失败则返回无效值
    */
    static PasswordHashQueryResult getUserInfoForLoginByUsername(const std::string& username);
    
    /*
        通过username修改用户online状态: 0表示离线，1表示在线。
    */
    static bool updateOnlineStatusByUsername(const std::string& username,int status);
};
