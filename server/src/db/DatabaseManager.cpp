#include "DatabaseManager.h"
#include <iostream>
#include <cstdlib>

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
        // 环境变量允许临时指定配置文件
        // 优先读取环境变量CHATROOM_DB_CONFIG，没有设置时用CMake配置的默认路径
        const char *configPath = std::getenv("CHATROOM_DB_CONFIG");
#ifdef CHATROOM_DB_CONFIG_FILE
        const std::string defaultConfigPath = CHATROOM_DB_CONFIG_FILE;
#else
        const std::string defaultConfigPath = "config/database.conf";
#endif
        std::string configFile = configPath != nullptr && configPath[0] != '\0' ? configPath : defaultConfigPath;
        DBconfig config = DatabaseConfig::loadDBConfig(configFile);
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

bool DatabaseManager::beginTransaction()
{
    if (!isConnected())
    {
        m_lastErrorCode = -1;
        return false;
    }
    try
    {
        m_conn->setAutoCommit(false);
        return true;
    }
    catch (sql::SQLException &e)
    {
        m_lastErrorCode = e.getErrorCode();
        std::cerr << "开启数据库事务失败: " << e.what() << '\n';
        return false;
    }
}

bool DatabaseManager::commitTransaction()
{
    if (!isConnected())
    {
        m_lastErrorCode = -1;
        return false;
    }
    try
    {
        m_conn->commit();
        return true;
    }
    catch (sql::SQLException &e)
    {
        m_lastErrorCode = e.getErrorCode();
        std::cerr << "提交数据库事务失败: " << e.what() << '\n';
        return false;
    }
}

void DatabaseManager::rollbackTransaction() noexcept
{
    if (!isConnected())
    {
        return;
    }
    try
    {
        m_conn->rollback();
    }
    catch (const std::exception &e)
    {
        std::cerr << "回滚数据库事务失败: " << e.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "回滚数据库事务时发生未知异常\n";
    }
}

int DatabaseManager::lastErrorCode() const
{
    return m_lastErrorCode;
}

void DatabaseManager::bindOne(sql::PreparedStatement &pstmt, int index, int value)
{
    pstmt.setInt(index, value);
}

void DatabaseManager::bindOne(sql::PreparedStatement &pstmt, int index, std::uint64_t value)
{
    // 用十进制字符串绑定，避免 uint64_t 转化成有符号 int64_t。
    // MariaDB 会根据目标 BIGINT UNSIGNED 列进行数字转换。
    pstmt.setString(index, std::to_string(value));
}

void DatabaseManager::bindOne(sql::PreparedStatement &pstmt, int index, double value)
{
    pstmt.setDouble(index, value);
}

void DatabaseManager::bindOne(sql::PreparedStatement &pstmt, int index, const std::string &value)
{
    pstmt.setString(index, value);
}
