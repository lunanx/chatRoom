#pragma once

#include "protocol/ProtocolHeader.h"
#include "DatabaseConfig.h"
#include <mariadb/conncpp.hpp>
#include "protocol/ProtocolBody.h"
#include <iostream>
#include <memory>
#include <cstdint>
#include <string>

/*
    查询结果需要同时保留 PreparedStatement 和 ResultSet。
    MariaDB Connector/C++ 的 ResultSet 可能依赖创建它的 Statement，
 */
struct DatabaseQueryResult
{
    std::unique_ptr<sql::PreparedStatement> statement;
    std::unique_ptr<sql::ResultSet> result;
    // true 表示 SQL 查询已成功执行；结果集为空不代表查询失败。
    bool succeeded{false};
    int errorCode{0};
};

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    // 连接管理
    bool connect();
    void disconnect();
    bool isConnected() const;

    // 事务接口: 同一事务中的查询和更新必须复用同一个DatabaseManager
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction() noexcept;

    // 返回最近一次数据库操作的错误码；成功时为 0，非 SQL 错误时可能为 -1。
    int lastErrorCode() const;

    // 执行增删改sql语句,返回受影响的行数;发生错误时返回-1
    template <typename... Args>
    int executeUpdate(const std::string &sql, const Args &...args);

    // 执行查sql语句。返回对象会同时持有 PreparedStatement 和 ResultSet
    template <typename... Args>
    DatabaseQueryResult query(const std::string &sql, const Args &...args);

    // 只判断查询结果是否至少有一行。数据库错误目前也会返回false
    template <typename... Args>
    bool exisit(const std::string &sql, const Args &...args);

private:
    // 根据参数类型选择对应的数据库参数绑定函数
    void bindOne(sql::PreparedStatement &pstmt, int index, int value);
    void bindOne(sql::PreparedStatement &pstmt, int index, std::uint64_t value);
    void bindOne(sql::PreparedStatement &pstmt, int index, double value);
    void bindOne(sql::PreparedStatement &pstmt, int index, const std::string &value);

    // 折叠表达式：按顺序把可变参数绑定到sql中的 ？占位符
    template <typename... Args>
    void bindParameters(sql::PreparedStatement &pstmt, const Args &...args);

    std::unique_ptr<sql::Connection> m_conn;
    int m_lastErrorCode{0};
};

// 模块定义必须在调用它们的编译单元中可见，所以放在头文件中。
template <typename... Args>
inline int DatabaseManager::executeUpdate(const std::string &sql, const Args &...args)
{
    m_lastErrorCode = 0;
    if (!isConnected())
    {
        std::cerr << "数据库连接不可用，无法执行更新语句\n";
        m_lastErrorCode = -1;
        return -1;
    }

    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt(m_conn->prepareStatement(sql));

        bindParameters(*pstmt, args...); // 参数绑定

        return pstmt->executeUpdate();
    }
    catch (sql::SQLException &e)
    {
        m_lastErrorCode = e.getErrorCode();
        std::cerr << "SQL更新失败: " << e.what()
                  << "，错误代码:" << e.getErrorCode() << '\n';
        return -1;
    }
}

template <typename... Args>
inline DatabaseQueryResult DatabaseManager::query(const std::string &sql, const Args &...args)
{
    DatabaseQueryResult queryResult;
    m_lastErrorCode = 0;
    if (!isConnected())
    {
        std::cerr << "数据库连接不可用，无法执行查询语句\n";
        m_lastErrorCode = -1;
        queryResult.errorCode = -1;
        return queryResult;
    }

    try
    {
        queryResult.statement.reset(m_conn->prepareStatement(sql));

        bindParameters(*queryResult.statement, args...); // 绑定参数

        queryResult.result.reset(queryResult.statement->executeQuery());
        queryResult.succeeded = true;

        return queryResult;
    }
    catch (sql::SQLException &e)
    {
        m_lastErrorCode = e.getErrorCode();
        queryResult.errorCode = e.getErrorCode();
        std::cerr << "SQL更新失败: " << e.what()
                  << "，错误代码:" << e.getErrorCode() << '\n';
        queryResult.result.reset();
        queryResult.statement.reset();
        return DatabaseQueryResult{};
    }
}

template <typename... Args>
inline bool DatabaseManager::exisit(const std::string &sql, const Args &...args)
{
    DatabaseQueryResult queryResult = query(sql, args...);
    // SQL查询语句执行失败并且结果集为空
    if (!queryResult.succeeded || !queryResult.result)
    {
        return false;
    }

    try
    {
        // SQL语句执行成功，但没有结果，就返回false
        return queryResult.result->next();
    }
    catch (sql::SQLException &e)
    {
        m_lastErrorCode = e.getErrorCode();
        std::cerr << "读取查询结果失败: " << e.what()
                  << "，错误代码:" << e.getErrorCode() << '\n';
        return false;
    }
}

template <typename... Args>
inline void DatabaseManager::bindParameters(sql::PreparedStatement &pstmt, const Args &...args)
{
    int index = 1;
    (bindOne(pstmt, index++, args), ...);
}
