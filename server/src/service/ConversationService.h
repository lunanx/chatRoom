#pragma once

#include "protocol/ProtocolHeader.h"
#include "repository/ConversationRepository.h"
#include "model/Conversation.h"
#include "db/DatabaseManager.h"
#include <string>

struct ConversationCreateResult
{
    protocolHeader::CommandType command;
    std::optional<std::uint64_t> conversationId;
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
    */
    static ConversationCreateResult createConversation(const std::string &body, std::string &responseBody);

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
        Conversation类，但Conversation类的last_seq不存在
    */
    static ConversationCreateRequestResult validateConversationCreateRequest(const std::string &body, nlohmann::json &data);

    /*
        检查type是否符合要求
        检查内容:
        要么是1,要么是2;
    */
    static bool isValidType(const std::uint64_t &type, std::uint64_t check);
    /*
        检查userId是否符合要求
        检查内容:
        当前不知检查什么，默认直接返回true;
    */
    static bool isValidUserId(const std::uint64_t &userId);

    /*
        检查groupId是否符合要求
        检查内容:
        当前不知检查什么，默认直接返回true;
    */
    static bool isValidGroupId(const std::uint64_t &groupId);
};
