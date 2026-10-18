#pragma once

#include "db/DatabaseManager.h"
#include "model/Message.h"
#include <optional>

enum class CreateMessageStatus
{
    Created,
    Duplicate, // Duplicate 表示同一个 client_msg_id 的相同请求已成功保存，可以返回原结果但不重复广播。
    ClientMsgIdConflict,
    ConversationNotFoundOrForbidden,
    Failed
};

struct CreateMessageResult
{
    CreateMessageStatus status{CreateMessageStatus::Failed};
    std::optional<Message> message;
};

enum class QueryHistoricalMessageStatus
{
    Error,
    NotFound,
    Found
};

struct QueryHistoricalMessageResult
{
    QueryHistoricalMessageStatus status{QueryHistoricalMessageStatus::Error};
    std::optional<std::array<std::string,5>> contents;
};

class MessageRepository
{
public:
    /*
        在一个事务内校验会话成员、分配 seq、插入消息并更新 Conversation.last_seq。
    */
    static CreateMessageResult createMessage(const Message &message);

    /*
        查询conversation_id下的历史消息，单次查询5个
    */
    static QueryHistoricalMessageResult queryHistoricalMessage(const std::uint64_t conversation_id, const std::uint64_t seq);
};
