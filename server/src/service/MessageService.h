#pragma once

#include "protocol/ProtocolHeader.h"
#include "repository/MessageRepository.h"
#include "security/PasswordHasher.h"
#include "model/Message.h"
#include "db/DatabaseManager.h"
#include <string>
#include <optional>

struct MessageSendResult
{
    protocolHeader::CommandType command;
    std::optional<Message> message;
};

class MessageService
{
public:
    /*
        发送消息业务
    */
    static MessageSendResult sendContent(const std::string &body, std::string &responseBody, const std::uint64_t &userId, const std::string &username);

private:
    /*
        检查消息发送请求的JSON是否符合要求
        检查内容:
        JSON语法检查
            ↓
        content 是否存在 + 类型是否正确
    */
    static bool validateSendContentRequest(const std::string &body, nlohmann::json &data);

    /*
        检查消息是否符合要求
        检查内容:
        是否为空
        大小是否<=2048;
    */
    static bool isValidContent(const std::string &content);
};
