#pragma once

#include "protocol/ProtocolHeader.h"
#include "protocol/ProtocolBody.h"
#include "repository/ConversationRepository.h"
#include "repository/MessageRepository.h"
#include "security/PasswordHasher.h"
#include "model/Message.h"
#include "db/DatabaseManager.h"
#include <string>
#include <optional>
#include <array>

constexpr int HISTORYMESSAGECNT = 5;

struct MessageSendResult
{
    protocolHeader::CommandType command{protocolHeader::CommandType::MESSAGE_SEND_FAILED};
    std::optional<Message> message;
    bool shouldBroadcast{false};
};

class MessageService
{
public:
    /*
        发送消息业务
        认证用户身份由 Dispatcher 从 ClientSession 传入，
    */
    static MessageSendResult sendContent(
        const std::string &body,
        std::string &responseBody,
        const std::uint64_t &userId,
        const std::string &username);
    /*
        查询历史消息业务
        认证用户身份由 Dispatcher 从 ClientSession 传入，
        body有需要查询历史消息的会话id、和从哪里开始查询的seq,一次查询5个
    */
    static protocolHeader::CommandType MessageHistoryQuery(const std::string &body, std::string &responseBody, std::uint64_t userId);

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
        检查查询历史消息请求的JSON是否符合要求
        检查内容:
        JSON语法检查
            ↓
        conversation_id是否存在+类型正确
    */
    static bool validateHistoricalMessageQueryRequest(const std::string &body, nlohmann::json &data);

    /*
        检查消息是否符合要求
        检查内容:
        是否为空
        大小是否<=2048;
    */
    static bool isValidContent(const std::string &content);

    /*
        检查conversation_id是否符合要求
        检查内容:
        是否 > 0
    */
    static bool isValidConversationId(const std::uint64_t &conversationId);

    /*
        检查client_msg_id是否符合要求
        检查内容:
        是否为空
        是否超过了64字节
    */
    static bool isValidClientMsgId(const std::string &clientMsgId);

    /*
        检查seq是否符合要求
        检查内容:
        是否 > 0
    */
    static bool isValidSeq(const std::uint64_t &seq);
};
