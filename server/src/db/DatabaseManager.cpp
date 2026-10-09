#include "DatabaseManager.h"
#include <iostream>
#include <memory>
#include <string>

std::unique_ptr<sql::Connection> DatabaseManager::createConnection()
{
    DBconfig config = DatabaseConfig::loadDBConfig("config/database.conf");
    try
    {
        // 获取MariaDB驱动
        sql::Driver *driver = sql::mariadb::get_driver_instance();
        // 配置数据库连接地址
        sql::SQLString url(
            "jdbc:mariadb://" + config.host + ":" +
            std::to_string(config.port) + "/" + config.database);
        // 配置连接账号
        sql::Properties properties({{"user", config.username},
                                    {"password", config.password}});
        // 创建数据库连接
        std::unique_ptr<sql::Connection> conn(
            driver->connect(url, properties));
        printf("数据库连接成功!\n");
        return conn;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "数据库错误：" << e.what() << '\n';
        std::cerr << "错误代码：" << e.getErrorCode() << '\n';
        return nullptr;
    }
    catch (const std::exception& e)
    {
        std::cerr << "程序异常：" << e.what() << '\n';
        return nullptr;
    }
}
