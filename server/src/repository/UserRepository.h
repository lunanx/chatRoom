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
        创建用户: 数据库中的username唯一约束是防止重复注册的最终保障
    */
    static bool createUser(const User& user);

    /*
        获取可登录账户的密码哈希: 用户不存在或账户状态不是正常时返回空字符串
    */
    static std::string getPasswordHashByUsername(const std::string& username);
    
    /*
        通过username修改用户online状态: 0表示离线，1表示在线。
    */
    static bool updateOnlineStatusByUsername(const std::string& username,int status);
};
