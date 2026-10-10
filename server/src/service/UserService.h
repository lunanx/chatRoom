#pragma once
#include "protocol/ProtocolHeader.h"
#include "repository/UserRepository.h"
#include "security/PasswordHasher.h"
#include "model/User.h"
#include "db/DatabaseManager.h"
#include <string>

class UserService
{

public:
    /*
        注册业务
        这里先留着responseBody参数
        由于body正文部分的格式还没确定，这里先不着急,继续写架构
    */
    static protocolHeader::CommandType registerUser(const std::string &body, std::string &responseBody);

    /*
        登录业务
    */
    static protocolHeader::CommandType loginUser(const std::string &body, std::string &responseBody);

    /*
        退出登录业务
    */
    static protocolHeader::CommandType logoutUser(const std::string &body, std::string &responseBody);

private:
    /*
        检查注册请求的JSON是否符合要求
        检查内容:
        JSON语法检查
            ↓
        username 是否存在 + 类型是否正确
            ↓
        password 是否存在 + 类型是否正确
            ↓
        nickname 是否存在 + 类型是否正确
            ↓
        avatar 如果存在，类型是否正确

    */
    static bool validateRegisterRequest(const std::string &body, nlohmann::json &data);

    /*
        检查登录请求的JSON是否符合要求
        检查内容:
        JSON语法检查
            ↓
        username 是否存在 + 类型是否正确
            ↓
        password 是否存在 + 类型是否正确
    */
    static bool validateLoginRequest(const std::string &body, nlohmann::json &data);
    /*
        检查用户名是否符合要求
        检查内容:
        长度在3~32
        内容只能是A-Z,a-z,0-9;
        #include <regex> 正则表达式同文件可以进行简易验证
    */
    static bool isValidUsername(const std::string &username);

    /*
        检查密码是否符合要求
        检查内容:(这一版先简单)
        长度在8~64
    */
    static bool isValidPassword(const std::string &password);

    /*
        检查昵称nickname是否符合要求
        检查内容:同样这一版简单
        昵称是否为空
        大小不超过64字节
    */
    static bool isValidNickname(const std::string &nickname);

    /*
        检查头像资源地址是否符合要求
        检查内容:
        是否为空
        大小是否<=1024;
    */
    static bool isValidAvatar(const std::string &avatar);
};
