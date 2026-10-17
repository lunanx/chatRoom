#include "DatabaseManager.h"
#include <iostream>

DatabaseManager::DatabaseManager() = default;

DatabaseManager::~DatabaseManager()
{
    disconnect();
}

bool DatabaseManager::connect()
{
    if (isConnected())
    {
        return true;
    }

    m_connectionLease = DatabaseConnectionPool::instance().acquire();
    if (!m_connectionLease)
    {
        m_lastErrorCode = -1;
        std::cerr << "无法从数据库连接池获取连接（连接创建失败或等待超时）\n";
        return false;
    }

    m_lastErrorCode = 0;
    return true;
}

void DatabaseManager::disconnect()
{
    // 释放 Lease 即归还物理连接；不直接 close，因为连接由连接池管理。
    m_connectionLease = DatabaseConnectionPool::Lease{};
}

bool DatabaseManager::isConnected() const
{
    if (!m_connectionLease || m_connectionLease.get() == nullptr)
    {
        return false;
    }

    try
    {
        return !m_connectionLease.get()->isClosed();
    }
    catch (...)
    {
        return false;
    }
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
        m_connectionLease.get()->setAutoCommit(false);
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
        m_connectionLease.get()->commit();
    }
    catch (sql::SQLException &e)
    {
        m_lastErrorCode = e.getErrorCode();
        discardIfConnectionBroken(m_lastErrorCode);
        std::cerr << "提交数据库事务失败: " << e.what() << '\n';
        return false;
    }

    // commit 已成功后，即使恢复 autocommit 失败，也不能谎报事务未提交。
    // 此时丢弃连接，避免把状态不明的连接交给下一个请求。
    try
    {
        m_connectionLease.get()->setAutoCommit(true);
    }
    catch (sql::SQLException e)
    {
        m_lastErrorCode = e.getErrorCode();
        std::cerr << "事务已提交，但恢复 autocommit 失败，将丢弃连接: "
                  << e.what() << '\n';
        m_connectionLease.discard();
    }
    return true;
}

void DatabaseManager::rollbackTransaction() noexcept
{
    if (!isConnected())
    {
        return;
    }
    try
    {
        m_connectionLease.get()->rollback();
        m_connectionLease.get()->setAutoCommit(true);
    }
    catch (const std::exception &e)
    {
        std::cerr << "回滚数据库事务失败，将丢弃连接: " << e.what() << '\n';
        m_connectionLease.discard();
    }
    catch (...)
    {
        std::cerr << "回滚数据库事务时发生未知异常，将丢弃连接\n";
        m_connectionLease.discard();
    }
}

int DatabaseManager::lastErrorCode() const
{
    return m_lastErrorCode;
}

void DatabaseManager::discardIfConnectionBroken(int errorCode) noexcept
{
    // 常见 MySQL/MariaDB 连接失效错误：server gone away、lost connection。
    // 其他错误（例如重复键、SQL 语法错误）通常不意味着物理连接损坏。
    if (errorCode == 2006 || errorCode == 2013 || errorCode == 2055 || errorCode == 1927)
    {
        m_connectionLease.discard();
    }
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
