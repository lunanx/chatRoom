#include "DatabaseManager.h"
#include <iostream>

DatabaseManager::DatabaseManager() = default;

DatabaseManager::~DatabaseManager()
{
    disconnect();
}

bool DatabaseManager::connect()
{

    try
    {
        // 获取configDB基础数据
        DBconfig config = DatabaseConfig::loadDBConfig("config/database.conf");
        // 获取MariaDB驱动
        sql::Driver *driver = sql::mariadb::get_driver_instance();
        // 配置数据库连接地址
        sql::SQLString url(
            "jdbc:mariadb://" + config.host + ":" +
            std::to_string(config.port) + "/" + config.database);
        // 配置连接账号
        sql::Properties properties({{"user", config.username},
                                    {"password", config.password}});
        // 连接数据库
        m_conn.reset(driver->connect(url, properties));
        return m_conn != nullptr;
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "数据库错误：" << e.what() << '\n';
        std::cerr << "错误代码：" << e.getErrorCode() << '\n';
        m_conn.reset();
        return false;
    }
    catch (const std::exception &e)
    {
        std::cerr << "程序异常：" << e.what() << '\n';
        m_conn.reset();
        return false;
    }
}

void DatabaseManager::disconnect()
{
    if (m_conn)
    {
        try
        {
            m_conn->close();
        }
        catch (...)
        {
            // 析构和断开连接时避免异常向外传播
        }

        m_conn.reset();
    }
}

bool DatabaseManager::isConnected() const
{
    return m_conn && !m_conn->isClosed();
}


void DatabaseManager::bindOne(sql::PreparedStatement &pstmt, int index, int value)
{
    pstmt.setInt(index, value);
}

void DatabaseManager::bindOne(sql::PreparedStatement &pstmt, int index, double value)
{
    pstmt.setDouble(index, value);
}

void DatabaseManager::bindOne(sql::PreparedStatement &pstmt, int index, const std::string &value)
{
    pstmt.setString(index, value);
}
