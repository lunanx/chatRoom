#pragma once

#include "db/DatabaseManager.h"
#include "model/Conversation.h"
#include <cstdint>

enum class ConversationQueryStatus
{
    Found,
    NotFound,
    Error
};

struct getLastSeqResult
{
    ConversationQueryStatus status{ConversationQueryStatus::Error};
    std::uint64_t lastSeq{0};
};

struct getConversationIdResult
{
    ConversationQueryStatus status{ConversationQueryStatus::Error};
    std::uint64_t conversationId{0};
};

enum class CreateConversationResult
{
    Created,
    Failed
};

class ConversationRepository
{
public:
    /*
        创建私聊
    */
    static CreateConversationResult createPrivateConversation(const Conversation &conversation);

    /*
        创建群聊
    */
    static CreateConversationResult createGroupConversation(const Conversation &conversation);

    /*
        获取会话内最后一条已提交消息的序号。
    */
    static getLastSeqResult getLastSeqByConversationId(std::uint64_t conversationId);

    /*
        根据私聊双方 ID 或群组 ID 查找已有 Conversation。
    */
    static getConversationIdResult getConversationIdByConversationObject(const Conversation &conversation);

    /*
        仅当群组存在且状态正常，并且当前用户在 GroupMember 中时返回 Found。
    */
    static ConversationQueryStatus checkActiveGroupMember(std::uint64_t groupId, std::uint64_t userId);
};
