#pragma once

#include "db/DatabaseManager.h"
#include "model/User.h"
#include <string>

class UserRepository
{
public:
    /*
        通过username查询用户是否存在
    */
    static bool findByUsername(const std::string& username);

    /*
        创建用户
    */
    static bool createUser(const User& user);

    /*
        获取已存在username的hash密码
    */
    static std::unique_ptr<sql::ResultSet> getPasswordHashByUsername(const std::string& username);
    
    /*
        通过username修改用户online状态
    */
    static bool updateOnlineStatusByUsername(const std::string& username,int status);
};
