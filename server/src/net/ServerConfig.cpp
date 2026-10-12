#include "ServerConfig.h"

#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace
{
    std::string trim(const std::string& value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }

        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }
}

ServerConfig ServerConfig::loadServerConfig(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("无法打开服务器配置文件：" + path);
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos)
        {
            throw std::runtime_error("服务器配置文件格式错误：" + line);
        }

        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (key.empty() || value.empty())
        {
            throw std::runtime_error("服务器配置项的键和值都不能为空：" + line);
        }

        if (!values.emplace(key, value).second)
        {
            throw std::runtime_error("服务器配置项重复：" + key);
        }
    }

    const auto hostIt = values.find("SERVER_HOST");
    const auto portIt = values.find("SERVER_PORT");
    if (hostIt == values.end() || portIt == values.end())
    {
        throw std::runtime_error("服务器配置必须包含 SERVER_HOST 和 SERVER_PORT");
    }

    std::size_t parsedLength = 0;
    unsigned long portValue = 0;
    try
    {
        portValue = std::stoul(portIt->second, &parsedLength);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("SERVER_PORT 必须是 1 到 65535 之间的整数");
    }

    if (parsedLength != portIt->second.size() || portValue == 0 || portValue > 65535)
    {
        throw std::runtime_error("SERVER_PORT 必须是 1 到 65535 之间的整数");
    }

    ServerConfig config;
    config.host = hostIt->second;
    config.port = static_cast<std::uint16_t>(portValue);
    return config;
}
