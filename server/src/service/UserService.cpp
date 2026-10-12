#include "UserService.h"
#include "protocol/ProtocolBody.h"
#include <string>
#include <regex>

/*
目前响应格式统一为
{
  "code": 8,
  "message": "注册成功"
}
*/
namespace
{
    // JSON 解析器会检查 UTF-8；因此统计非 UTF-8 延续字节的数量即可得到 Unicode 码点数。
    // 这比直接使用 std::string::size() 更接近普通用户理解的“字符数”。
    std::size_t utf8CharacterCount(const std::string &value)
    {
        std::size_t count = 0;
        for (unsigned char ch : value)
        {
            // 多字节的非首个字节的前两位都是 10
            if ((ch & 0xC0) != 0x80)
            {
                ++count;
            }
        }
        return count;
    }

    // 统一生成服务器响应Body;以后替换响应格式只需要集中修改这里
    protocolHeader::CommandType setResponse(
        std::string &responseBody,
        protocolHeader::CommandType command,
        const std::string &message)
    {
        const nlohmann::json response = {
            {protocolHeader::CODE, static_cast<std::uint16_t>(command)},
            {protocolHeader::MESSAGE, message}};

        responseBody = response.dump();
        return command;
    }
}

protocolHeader::CommandType UserService::registerUser(
    const std::string &body,
    std::string &responseBody)
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
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "注册请求格式不正确");
    }

    // 检查username
    std::string username = data[protocolHeader::USERNAME].get<std::string>();
    if (!isValidUsername(username))
    {
        printf("用户名校验不通过,注册失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "用户名格式不正确");
    }

    // 检查password
    std::string password = data[protocolHeader::PASSWORD].get<std::string>();
    if (!isValidPassword(password))
    {
        printf("密码校验不通过,注册失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "密码长度必须为8~64个字符,或密码格式不正确");
    }
    // 检查昵称
    std::string nickname = data[protocolHeader::NICKNAME].get<std::string>();
    if (!isValidNickname(nickname))
    {
        printf("昵称校验不通过,注册失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "昵称不能为空且不能超过64个字符");
    }
    // 检查头像资源地址
    std::string avatar;
    if (data.contains(protocolHeader::AVATAR) && !data[protocolHeader::AVATAR].is_null())
    {
        avatar = data[protocolHeader::AVATAR].get<std::string>();
        if (!isValidAvatar(avatar))
        {
            printf("头像资源地址校验不通过,注册失败\n");
            return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "头像地址不能超过1024个字符");
        }
    }

    // 检查用户名是否存在
    const UserQueryStatus usernameStatus = UserRepository::findByUsername(username);
    if (usernameStatus == UserQueryStatus::Found)
    {
        printf("用户名已存在,注册失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "用户名已存在");
    }
    if (usernameStatus == UserQueryStatus::Error)
    {
        printf("查询用户名失败，注册暂时无法完成\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "服务器暂时无法处理注册请求，请稍后重试");
    }

    // 密码进行哈希转换
    std::string passwordHash;
    if (!PasswordHasher::hashPassword(password, passwordHash))
    {
        printf("密码哈希转换失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "服务器暂时无法处理注册请求，请稍后重试");
    }
    // 构造User
    User user(username, passwordHash, nickname, avatar);
    // 保存至数据库。预检查与插入之间可能有另一个客户端抢先注册，所以插入结果仍需分类处理。
    const CreateUserResult createResult = UserRepository::createUser(user);
    if (createResult == CreateUserResult::UsernameExists)
    {
        printf("插入用户时遇到用户名唯一键冲突\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "用户名已存在");
    }
    if (createResult != CreateUserResult::Created)
    {
        printf("数据库创建用户失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::REGISTER_FAILED, "服务器暂时无法处理注册请求，请稍后重试");
    }
    printf("注册成功\n");
    return setResponse(responseBody, protocolHeader::CommandType::REGISTER_SUCCESS, "注册成功");
}

protocolHeader::CommandType UserService::loginUser(const std::string &body, std::string &responseBody)
{
    nlohmann::json data;
    // 校验JSON、username、password
    if (!validateLoginRequest(body, data))
    {
        printf("数据类型校验不通过,登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGIN_FAILED, "登录请求格式不正确");
    }
    // 检查username
    std::string username = data[protocolHeader::USERNAME].get<std::string>();
    if (!isValidUsername(username))
    {
        printf("用户名校验不通过,登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGIN_FAILED, "用户名或密码错误");
    }
    // 检查password
    std::string password = data[protocolHeader::PASSWORD].get<std::string>();
    if (!isValidPassword(password))
    {
        printf("密码校验不通过,登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGIN_FAILED, "用户名或密码错误");
    }

    // 区分“账号不可用”和“数据库查询失败”，避免把服务器故障误报为账号不存在。
    const PasswordHashQueryResult hashResult = UserRepository::getPasswordHashByUsername(username);
    if (hashResult.status == UserQueryStatus::Error)
    {
        printf("查询密码哈希失败，登录暂时无法完成\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGIN_FAILED, "服务器暂时无法处理登录请求，请稍后重试");
    }
    if (hashResult.status == UserQueryStatus::NotFound ||
        !PasswordHasher::verifyPassword(password, hashResult.passwordHash))
    {
        printf("用户名或密码错误，登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGIN_FAILED, "用户名或密码错误");
    }

    // 密码正确，更新用户在线状态，更新失败不向客户端报告登录成功
    if (!UserRepository::updateOnlineStatusByUsername(username, 1))
    {
        printf("更新用户在线状态失败，登录失败");
        return setResponse(responseBody, protocolHeader::CommandType::LOGIN_FAILED, "服务器暂时无法处理登录请求，请稍后重试");
    }

    printf("账户密码正确，登录成功\n");
    return setResponse(responseBody, protocolHeader::CommandType::LOGIN_SUCCESS, "登录成功");
}

protocolHeader::CommandType UserService::logoutUser(const std::string &body, std::string &responseBody)
{
    nlohmann::json data;
    // 校验JSON、username、password
    if (!validateLoginRequest(body, data))
    {
        printf("数据类型校验不通过,退出登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGOUT_FAILED, "退出登录请求格式不正确");
    }

    // 检查username
    std::string username = data[protocolHeader::USERNAME].get<std::string>();
    if (!isValidUsername(username))
    {
        printf("用户名校验不通过,退出登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGOUT_FAILED, "用户名或密码错误");
    }
    // 检查password
    std::string password = data[protocolHeader::PASSWORD].get<std::string>();
    if (!isValidPassword(password))
    {
        printf("密码校验不通过,退出登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGOUT_FAILED, "用户名或密码错误");
    }

    // 当前最小实现用账户密码验证退出请求。后续应将登录身份绑定到 ClientSession
    // 再由连接身份完成退出，而不是每次退出都信任客户端提供的username
    const PasswordHashQueryResult hashResult = UserRepository::getPasswordHashByUsername(username);
    if (hashResult.status == UserQueryStatus::Error)
    {
        printf("查询密码哈希失败，退出登录暂时无法完成\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGOUT_FAILED, "服务器暂时无法处理登录请求，请稍后重试");
    }

    if (hashResult.status == UserQueryStatus::NotFound ||
        !PasswordHasher::verifyPassword(password, hashResult.passwordHash))
    {
        printf("账号验证失败，退出登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGOUT_FAILED, "用户名或密码错误");
    }

    if (!UserRepository::updateOnlineStatusByUsername(username, 0))
    {
        printf("更新用户在线状态失败，退出登录失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::LOGOUT_FAILED, "服务器暂时无法处理退出登录请求，请稍后再试");
    }

    printf("退出登录成功\n");
    return setResponse(responseBody, protocolHeader::CommandType::LOGOUT_SUCCESS, "退出登录成功");
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

    if (!data.is_object())
    {
        printf("JSON Body必须是对象\n");
        return false;
    }

    if (!data.contains(protocolHeader::USERNAME) || !data[protocolHeader::USERNAME].is_string())
    {
        printf("username 不存在或者不是字符串\n");
        return false;
    }

    if (!data.contains(protocolHeader::PASSWORD) || !data[protocolHeader::PASSWORD].is_string())
    {
        printf("password 不存在或者不是字符串\n");
        return false;
    }

    if (!data.contains(protocolHeader::NICKNAME) || !data[protocolHeader::NICKNAME].is_string())
    {
        printf("nickname 不存在或者不是字符串\n");
        return false;
    }

    if (data.contains(protocolHeader::AVATAR) && !data[protocolHeader::AVATAR].is_string() && !data[protocolHeader::AVATAR].is_null())
    {
        printf("avatar必须是字符串或null\n");
        return false;
    }

    return true;
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

    if (!data.is_object())
    {
        printf("JSON Body 必须是对象\n");
        return false;
    }

    if (!data.contains(protocolHeader::USERNAME) || !data[protocolHeader::USERNAME].is_string())
    {
        printf("username 不存在或者不是字符串\n");
        return false;
    }

    if (!data.contains(protocolHeader::PASSWORD) || !data[protocolHeader::PASSWORD].is_string())
    {
        printf("password 不存在或者不是字符串\n");
        return false;
    }

    return true;
}

bool UserService::isValidUsername(const std::string &username)
{
    //由于下面regex的作用不需要转换实际Unicode码点
    if (username.size() < 3 || username.size() > 32)
    {
        return false;
    }

    static const std::regex pattern("^[A-Za-z0-9_!@#$%&*]+$");
    return std::regex_match(username, pattern);
}

bool UserService::isValidPassword(const std::string &password)
{
    //由于下面regex的作用不需要转换实际Unicode码点
    if (password.size() < 8 || password.size() > 64)
    {
        return false;
    }

    static const std::regex pattern("^[A-Za-z0-9_!@#$%&*]+$");
    return std::regex_match(password, pattern);
}

bool UserService::isValidNickname(const std::string &nickname)
{
    //昵称需要转换为Unicode码点
    return !nickname.empty() && utf8CharacterCount(nickname) <= 64 &&nickname.size() <= 256;
}

bool UserService::isValidAvatar(const std::string &avatar)
{
    
    return avatar.empty() || (utf8CharacterCount(avatar) <= 1024 && avatar.size() <= 4096);
}
