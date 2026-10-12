#pragma once

#include "protocol/ProtocolHeader.h"
#include "DatabaseConfig.h"
#include <mariadb/conncpp.hpp>
#include "protocol/ProtocolBody.h"
#include <iostream>
#include <memory>
#include <string>

/*
 * 查询结果需要同时保留 PreparedStatement 和 ResultSet。
 * MariaDB Connector/C++ 的 ResultSet 可能依赖创建它的 Statement，
 * 因此不能只把 ResultSet 返回给调用者、却在 query() 返回时销毁 Statement。
 */
struct DatabaseQueryResult
{
    std::unique_ptr<sql::PreparedStatement> statement;
    std::unique_ptr<sql::ResultSet> result;
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
    void bindOne(sql::PreparedStatement &pstmt, int index, double value);
    void bindOne(sql::PreparedStatement &pstmt, int index, const std::string &value);

    // 折叠表达式：按顺序把可变参数绑定到sql中的 ？占位符
    template <typename... Args>
    void bindParameters(sql::PreparedStatement &pstmt, const Args &...args);

    std::unique_ptr<sql::Connection> m_conn;
};

// 模块定义必须在调用它们的编译单元中可见，所以放在头文件中。
template <typename... Args>
inline int DatabaseManager::executeUpdate(const std::string &sql, const Args &...args)
{
    if (!isConnected())
    {
        std::cerr << "数据库连接不可用，无法执行更新语句\n";
        return -1;
    }

    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt(m_conn->prepareStatement(sql));

        bindParameters(pstmt, args...); // 参数绑定

        return pstmt->executeUpdate();
    }
    catch (const sql::SQLException &e)
    {
        std::cerr << "SQL更新失败: " << e.what()
                  << "，错误代码:" << e.getErrorCode() << "\n";
        return -1;
    }
}

template <typename... Args>
inline DatabaseQueryResult DatabaseManager::query(const std::string &sql, const Args &...args)
{
    DatabaseQueryResult queryResult;

    if (!isConnected())
    {
        std::cerr << "数据库连接不可用，无法执行查询语句\n";
        return queryResult;
    }

    try
    {
        queryResult.statement.reset(m_conn->prepareStatement(sql));

        bindParameters(*queryResult.statement, args...); // 绑定参数

        queryResult.result.reset(queryResult.statement->executeQuery());

        return queryResult;
    }
    catch (const sql::SQLException &e)
    {
        std::cerr << "SQL更新失败: " << e.what()
                  << "，错误代码:" << e.getErrorCode() << "\n";
        return DatabaseQueryResult{};
    }
}

template <typename... Args>
inline bool DatabaseManager::exisit(const std::string &sql, const Args &...args)
{
    DatabaseQueryResult queryResult = query(sql,args...);
    return queryResult.result != nullptr && queryResult.result->next();
}

template <typename... Args>
inline void DatabaseManager::bindParameters(sql::PreparedStatement &pstmt, const Args &...args)
{
    int index = 1;
    (bindOne(pstmt, index++, args), ...);
}
