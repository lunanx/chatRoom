#include "DatabaseManager.h"
#include <iostream>

DatabaseManager::DatabaseManager() = default;

DatabaseManager::~DatabaseManager()
{
    disconnect();
}


bool DatabaseManager::Connection()
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

template <typename... Args>
int DatabaseManager::executeUpdate(const std::string &sql, const Args &...args)
{
    if (!isConnected)
        std::cout<<"数据库连接已断开\n";
        return -1;

    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt(m_conn->prepareStatement(sql));

        bindParameters(pstmt.get(), args...); // 参数绑定

        return pstmt->executeUpdate();
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "SQL 执行失败: " << e.what() << std::endl;
        return -1;
    }
}

template <typename... Args>
std::unique_ptr<sql::ResultSet> DatabaseManager::query(const std::string &sql, const Args &...args)
{
    if (!isConnected)
        return nullptr;

    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt(m_conn->prepareStatement(sql));

        bindParameters(pstmt, args); // 绑定参数

        return std::unique_ptr<sql::ResultSet>(pstmt->executeQuery());
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "SQL 执行失败: " << e.what() << std::endl;
        return nullptr;
    }
}

template <typename... Args>
bool DatabaseManager::exisit(const std::string &sql, const Args &...args)
{
    if (!isConnected)
    {
        std::cout<<"数据库断开\n";
        return false;
    }
    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt(m_conn->prepareStatement(sql));

        bindParameters(pstmt,args);

        std::unique_ptr<sql::ResultSet> rs(pstmt->executeQuery());

        if (rs->next())
        {

            return true;
        }
        else
        {
            std::cout<<"不存在\n";
            return false;
        }
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "SQL 执行失败: " << e.what() << std::endl;
        return false;
    }
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

template <typename... Args>
void DatabaseManager::bindParameters(sql::PreparedStatement &pstmt, const Args &...args)
{
    int index = 1;
    (bindOne(pstmt, index++, args), ...);
}

