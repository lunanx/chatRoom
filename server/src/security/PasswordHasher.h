#pragma once

#include <string>

class PasswordHasher
{
public:
    /*
      对用户密码进行哈希处理

      注意：
      这里不是对密码进行加密，
      而是使用 libsodium提供的密码哈希算法,生成不可逆的密码哈希值。

      生成的 passwordHash 中包含：
      - 使用的算法信息
      - 计算参数
      - 随机生成的 salt
      - 最终的密码哈希结果

      因此数据库中只需要保存 passwordHash，
      不需要保存用户的原始密码。
    */
    static bool hashPassword(const std::string &password, std::string &passwordHash);

    /*
        验证用户输入的密码是否正确。

        password：
            用户输入的原始密码。

        passwordHash：
            数据库中保存的密码哈希。

        libsodium 会自动从 passwordHash 中读取：
        - 算法
        - 参数
        - salt

        然后完成密码验证。
    */
    static bool verifyPassword(const std::string &password, const std::string &passwordHash);
};
