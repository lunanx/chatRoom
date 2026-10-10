#include "UserService.h"
#include <string>
#include <regex>
protocolHeader::CommandType UserService::registerUser(const std::string &body, std::string &responseBody)
{
    /*
        username,password,nickname,avatar
        TODO:
        1 校验JSON合不合法
        2 校验username,password,nickname,avatar
        3 用户名是否已经存在？
        4 密码进行哈希转换
        5 创建User对象
        6 UserRepository保存
        7 返回注册结果
    */

    nlohmann::json data;
    // 校验JSON、username、password、nickname、avatar数据格式是否合格
    if (!validateRegisterRequest(body, data))
    {
        printf("数据类型校验不通过,注册失败\n");
        return protocolHeader::CommandType::REGISTER_FAILED;
    }

    // 检查username
    std::string username = data["username"].get<std::string>();
    if (!isValidUsername(username))
    {
        printf("用户名校验不通过,注册失败\n");
        return protocolHeader::CommandType::REGISTER_FAILED;
    }
    // 检查password
    std::string password = data["password"].get<std::string>();
    if (!isValidPassword(password))
    {
        printf("密码校验不通过,注册失败\n");
        return protocolHeader::CommandType::REGISTER_FAILED;
    }
    // 检查昵称
    std::string nickname = data["nickname"].get<std::string>();
    if (!isValidNickname(nickname))
    {
        printf("昵称校验不通过,注册失败\n");
        return protocolHeader::CommandType::REGISTER_FAILED;
    }
    // 检查头像资源地址
    std::string avatar;
    if (data.contains("avatar") && !data["avatar"].is_null())
    {
        avatar = data["avatar"].get<std::string>();
        if (!isValidAvatar(avatar))
        {
            printf("头像资源地址校验不通过,注册失败\n");
            return protocolHeader::CommandType::REGISTER_FAILED;
        }
    }
    // 检查用户名是否存在
    if (UserRepository::findByUsername(username))
    {
        printf("用户名已存在,注册失败\n");
        return protocolHeader::CommandType::REGISTER_FAILED;
    }
    // 密码进行哈希转换
    std::string passwordHash;
    if (!PasswordHasher::hashPassword(password, passwordHash))
    {
        printf("密码哈希转换失败\n");
        return protocolHeader::CommandType::REGISTER_FAILED;
    }
    // 构造User
    User u(username, passwordHash, nickname, avatar);

    // 保存至数据库
    if (UserRepository::createUser(u))
    {
        printf("注册成功\n");
        return protocolHeader::CommandType::REGISTER_SUCCESS;
    }
    else
    {
        return protocolHeader::CommandType::REGISTER_FAILED;
    }
}

protocolHeader::CommandType UserService::loginUser(const std::string &body, std::string &responseBody)
{
    nlohmann::json data;
    // 校验JSON、username、password
    if (!validateLoginRequest(body, data))
    {
        printf("数据类型校验不通过,登录失败\n");
        return protocolHeader::CommandType::LOGIN_FAILED;
    }
    // 检查username
    std::string username = data["username"].get<std::string>();
    if (!isValidUsername(username))
    {
        printf("用户名校验不通过,登录失败\n");
        return protocolHeader::CommandType::LOGIN_FAILED;
    }
    // 检查password
    std::string password = data["password"].get<std::string>();
    if (!isValidPassword(password))
    {
        printf("密码校验不通过,登录失败\n");
        return protocolHeader::CommandType::LOGIN_FAILED;
    }
    // 检查用户名是否存在
    if (!UserRepository::findByUsername(username))
    {
        printf("用户名不存在,登录失败\n");
        return protocolHeader::CommandType::LOGIN_FAILED;
    }

    // 这里留一个检查账户状态status的repository接口

    // 获取对应的hash密码,用户名已经存在，那就默认密码肯定有
    std::unique_ptr<sql::ResultSet> rs = UserRepository::getPasswordHashByUsername(username);
    sql::SQLString PWDHash = rs->getString("password_hash");
    std::string passwordHash(PWDHash.c_str(), PWDHash.length());
    if (PasswordHasher::verifyPassword(password, passwordHash))
    {
        printf("账户密码正确,登录成功\n");
        return protocolHeader::CommandType::LOGIN_SUCCESS;
    }
    else
    {
        printf("账户密码错误,登录失败\n");
        return protocolHeader::CommandType::LOGIN_FAILED;
    }
}

protocolHeader::CommandType UserService::logoutUser(const std::string &body, std::string &responseBody)
{
    nlohmann::json data;
    // 校验JSON、username、password
    if (!validateLoginRequest(body, data))
    {
        printf("数据类型校验不通过,退出登录失败\n");
        return protocolHeader::CommandType::LOGOUT_FAILED;
    }
    // 检查username
    std::string username = data["username"].get<std::string>();
    if (!isValidUsername(username))
    {
        printf("用户名校验不通过,退出登录失败\n");
        return protocolHeader::CommandType::LOGOUT_FAILED;
    }
    // 检查用户名是否存在，验证防止错误信息
    if (!UserRepository::findByUsername(username))
    {
        printf("用户名不存在,退出登录失败\n");
        return protocolHeader::CommandType::LOGOUT_FAILED;
    }
    if (UserRepository::updateOnlineStatusByUsername(username, 0))
    {
        printf("退出登录成功\n");
        return protocolHeader::CommandType::LOGOUT_SUCCESS;
    }
    else // 一般来说不会失败，但仍然检验
    {
        printf("退出登入失败\n");
        return protocolHeader::CommandType::LOGOUT_FAILED;
    }
}

bool UserService::validateRegisterRequest(const std::string &body, nlohmann::json &data)
{

    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        printf("JSON 格式错误\n");
        return false;
    }

    if (!data.contains("username") || !data["username"].is_string())
    {
        printf("username 不存在 或者 存在,但不是字符串\n");
        return false;
    }

    if (!data.contains("password") || !data["password"].is_string())
    {
        printf("password 不存在 或者 存在,但不是字符串\n");
        return false;
    }

    if (!data.contains("nickname") || !data["nickname"].is_string())
    {
        printf("nickname 不存在 或者 存在,但不是字符串\n");
        return false;
    }

    if (data.contains("avatar") && !data["avatar"].is_string() && !data["avatar"].is_null())
    {
        printf("avatar 存在,但值不是字符串也不是null\n");
        return false;
    }

    return true;
}

bool UserService::isValidUsername(const std::string &username)
{
    if (username.size() < 3 || username.size() > 32)
    {
        return false;
    }

    static const std::regex pattern("^[A-Za-z0-9_]+$");
    return std::regex_match(username, pattern);
}

bool UserService::isValidPassword(const std::string &password)
{
    return password.size() >= 8 && password.size() <= 64;
}

bool UserService::isValidNickname(const std::string &nickname)
{
    if (nickname.empty())
    {
        return false;
    }

    if (nickname.size() > 64)
    {
        return false;
    }

    return true;
}

bool UserService::isValidAvatar(const std::string &avatar)
{
    return avatar.empty() || avatar.size() <= 1024;
}

bool UserService::validateLoginRequest(const std::string &body, nlohmann::json &data)
{
    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        printf("JSON 格式错误\n");
        return false;
    }

    if (!data.contains("username") || !data["username"].is_string())
    {
        printf("username 不存在 或者 存在,但不是字符串\n");
        return false;
    }

    if (!data.contains("password") || !data["password"].is_string())
    {
        printf("password 不存在 或者 存在,但不是字符串\n");
        return false;
    }

    return true;
}
