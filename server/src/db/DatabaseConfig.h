#pragma once

#include <string>
#include <cstdint>

struct DBconfig
{
    std::string host;
    std::uint16_t port = 3306;
    std::string database;
    std::string username;
    std::string password;
};


class DatabaseConfig
{
public:
    static DBconfig loadDBConfig(const std::string& path);
};


