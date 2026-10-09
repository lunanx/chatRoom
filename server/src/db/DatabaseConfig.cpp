#include "DatabaseConfig.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

DBconfig DatabaseConfig::loadDBConfig(const std::string &path)
{
    std::ifstream file(path);

    if (!file.is_open())
    {
        throw std::runtime_error(
            "无法打开数据库配置文件：" + path
        );
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;

    while (std::getline(file, line))
    {
        // 去除行首空格和制表符
        const auto first = line.find_first_not_of(" \t\r\n");

        if (first == std::string::npos || line[first] == '#')
        {
            continue;
        }

        const auto pos = line.find('=', first);

        if (pos == std::string::npos)
        {
            throw std::runtime_error("配置文件格式错误");
        }

        const auto keyEnd = line.find_last_not_of(" \t", pos - 1);
        const auto valueStart = line.find_first_not_of(" \t", pos + 1);

        const std::string key =
            line.substr(first, keyEnd - first + 1);

        const std::string value =
            valueStart == std::string::npos
                ? ""
                : line.substr(valueStart);

        if (values.count(key) != 0)
        {
            throw std::runtime_error("配置项重复：" + key);
        }

        values.emplace(key, value);
    }

    auto require = [&](const std::string& key) -> const std::string&
    {
        auto it = values.find(key);

        if (it == values.end() || it->second.empty())
        {
            throw std::runtime_error("缺少配置项：" + key);
        }

        return it->second;
    };

    DBconfig config;

    config.host = require("DB_HOST");
    config.database = require("DB_NAME");
    config.username = require("DB_USER");
    config.password = require("DB_PASSWORD");

    const std::string portText = require("DB_PORT");
    std::size_t parsed = 0;
    unsigned long port = std::stoul(portText, &parsed);

    if (parsed != portText.size() || port == 0 || port > 65535)
    {
        throw std::runtime_error("数据库端口无效");
    }

    config.port = static_cast<std::uint16_t>(port);

    if (config.password == "CHANGE_ME")
    {
        throw std::runtime_error("请先配置真实数据库密码");
    }

    return config;
}