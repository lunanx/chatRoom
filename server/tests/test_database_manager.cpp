#include "db/DatabaseManager.h"

#include <iostream>
#include <memory>

int main()
{
    DatabaseManager manager;

    std::cout << "开始测试数据库连接...\n";

    auto conn = manager.createConnection();

    if (conn == nullptr)
    {
        std::cerr << "[FAIL] 数据库连接失败\n";
        return 1;
    }

    if (conn->isClosed())
    {
        std::cerr << "[FAIL] 连接已关闭\n";
        return 1;
    }

    std::cout << "[PASS] 数据库连接成功\n";

    return 0;
}