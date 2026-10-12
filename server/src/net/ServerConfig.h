#pragma once

#include <cstdint>
#include <string>

struct ServerConfig
{
    // 0.0.0.0 表示监听本机所有 IPv4 网卡。
    std::string host;
    std::uint16_t port = 8888;

    static ServerConfig loadServerConfig(const std::string& path);
};
