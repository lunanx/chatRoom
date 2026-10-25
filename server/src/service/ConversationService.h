#pragma once

#include "protocol/ProtocolHeader.h"
#include "repository/ConversationRepository.h"
#include "model/Conversation.h"
#include "db/DatabaseManager.h"
#include <cstdint>
#include <optional>
#include <string>

struct ConversationCreateResult
{
    protocolHeader::CommandType command{protocolHeader::CommandType::UNKNOWN};
    std::optional<std::uint64_t> conversationId;
    std::optional<std::uint64_t> lastSeq;
};

enum ConversationCreateRequestResult
{
    IsPrivateConversation,
    IsGroupConversation,
    RequestError
};

class ConversationService
{
public:
    /*
        创建会话业务
        存在就返回已有会话，不存在就创建新会话
        authenticatedUserId 必须来自 ClientSession，不能由客户端声明。
    */
    static ConversationCreateResult createConversation(
        const std::string &body,
        std::string &responseBody,
        const std::uint64_t &authenticatedUserId);

    /*
        查询当前认证用户可访问的私聊和群聊会话列表。
    */
    static protocolHeader::CommandType listConversations(
        std::string &responseBody,
        std::uint64_t authenticatedUserId);

private:
    /*
        检查会话请求的JSON是否符合要求
        检查内容:
        JSON语法检查
            ↓
        type是否存在 + 类型是否正确
            ↓
        userid1 和 userid2 是否都存在 + 类型是否正确 ->不存在检查GroupId是否存在 + 类型是否正确

        返回值:
        command,
        Conversation类，
        last_seq
    */
    static ConversationCreateRequestResult validateConversationCreateRequest(const std::string &body, nlohmann::json &data);
};
