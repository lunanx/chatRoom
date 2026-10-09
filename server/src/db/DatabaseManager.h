#pragma once

#include "protocol/ProtocolHeader.h"
#include "DatabaseConfig.h"
#include <mariadb/conncpp.hpp>


class DatabaseManager
{
public:
    /*
        当前阶段先每次调用时，都创建一个新连接
    */
    static std::unique_ptr<sql::Connection> createConnection();
    
};
