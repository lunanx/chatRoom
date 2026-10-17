#include "net/TcpServer.h"
#include <sodium.h>
#include <iostream>

int main()
{
    if (sodium_init() < 0)
    {
        std::cerr << "libsodium 初始化失败\n";
        return 1;
    }

    TcpServer server;
    if (!server.init())
    {
        std::cerr << "服务器初始化失败，请检查上方错误信息\n";
        return 1;
    }

    server.start();
    return 0;
}
