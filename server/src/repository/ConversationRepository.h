#pragma once

#include "db/DatabaseManager.h"
#include "model/Conversation.h"
#include <string>


enum class ConversationQueryStatus
{
    Found,
    NotFound,
    Error
};

struct getLastSeqResult
{
    ConversationQueryStatus status{ConversationQueryStatus::Error};
    std::uint64_t lastId;
};

struct getConversationIdResult
{
    ConversationQueryStatus status{ConversationQueryStatus::Error};
    std::uint64_t conversationId;
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
    static CreateConversationResult createPrivateConversation(const Conversation& conversation);

    /*
        创建群聊
    */
    static CreateConversationResult createGroupConversation(const Conversation& conversation);

    /*
        通过会话conversation_id更新last_seq
    */
    static bool updateLastSeqByConversationId(const std::uint64_t& conversation_id,const std::uint64_t& seq);

    /*
        通过会话conversation_id查询得到last_seq
    */
    static getLastSeqResult getLastSeqByConversationId(const std::uint64_t& conversation_id,const std::uint64_t& seq);
    
    /*
        通过其他字段查询得到conversation.id
    */
    static getConversationIdResult getConversationIdByConversationObject(const Conversation& conversation);
};

