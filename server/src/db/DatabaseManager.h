#pragma once

#include "protocol/ProtocolHeader.h"
#include "DatabaseConfig.h"
#include <mariadb/conncpp.hpp>
#include "protocol/ProtocolBody.h"
#include <iostream>
#include <memory>
#include <string>

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    // 连接管理
    bool Connection();
    void disconnect();
    bool isConnected() const;

    // 执行增删改sql语句
    // 规定只能增删改一行, 防止不知道是哪一个成功哪一个失败
    template <typename... Args>
    int executeUpdate(const std::string& sql,const Args&... args);
    
    // 执行查sql语句--需要获取数据
    template <typename... Args>
    std::unique_ptr<sql::ResultSet> query(const std::string& sql,const Args&... args);
    
    // 执行查sql语句---只是查询是否存在
    //  规定这里只能查询一行，防止不知道是哪一个存在哪一个不存在
    template <typename... Args>
    bool exisit(const std::string& sql,const Args&... args);

private:

    // 绑定一个参数
    void bindOne(
        sql::PreparedStatement& pstmt,
        int index,
        int value
    );
    void bindOne(
        sql::PreparedStatement& pstmt,
        int index,
        double value
    );
    void bindOne(
        sql::PreparedStatement& pstmt,
        int index,
        const std::string& value
    );
    // 折叠调用bindOne重载函数
    template <typename... Args>
    void bindParameters(sql::PreparedStatement& pstmt,const Args &...args);


    std::unique_ptr<sql::Connection> m_conn;
};

