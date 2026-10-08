#include "PasswordHasher.h"

#include <sodium.h>

bool PasswordHasher::hashPassword(const std::string &password, std::string &passwordHash)
{
    /*
        crypto_pwhash_STRBYTES
        是libsodium 为密码哈希字符串预留的缓冲区大小
    */
    char hash[crypto_pwhash_STRBYTES];

    /*
        使用 crypto_pwhash_str 生成密码哈希
        解释最后两个参数:
        crypto_pwhash_OPSLIMIT_INTERACTIVE: 设置密码哈希的计算强度。
        crypto_pwhash_MEMLIMIT_INTERACTIVE: 设置密码哈希使用的内存大小。
        其他参数信息可以查阅man手册
    */
    int res = crypto_pwhash_str(hash, password.data(), password.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE);
    if (res != 0)
    {
        return false;
    }

    /*
        将生成的密码哈希保存到passwordHash
    */
    passwordHash.assign(hash);

    /*
        清楚临时缓冲区中的敏感数据。

        使用 sodium_memzero()
        可以避免编译器优化掉内存清除操作
    */
    sodium_memzero(hash, sizeof(hash));

    return true;
}

bool PasswordHasher::verifyPassword(const std::string &password, const std::string &passwordHash)
{
    /*
        使用crypto_pwhash_str_verify 验证密码
        相关参数信息可以查询man手册
    */
    return crypto_pwhash_str_verify(passwordHash.c_str(), password.data(), password.size()) == 0;
}
