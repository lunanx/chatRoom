#pragma once

#include "db/DatabaseManager.h"
#include "model/Message.h"
#include <string>
#include <vector>
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
    Forbidden, // 当前用户无权访问这个会话
    NotFound,
    Found
};

//一条历史消息
struct HistoricalMessage
{
    std::uint64_t seq{0};
    std::string text;
};

struct QueryHistoricalMessageResult
{
    QueryHistoricalMessageStatus status{
        QueryHistoricalMessageStatus::Error
    };

    std::optional<std::vector<HistoricalMessage>> contents;
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
        seq = 0 表示获取最近的五条消息
        seq > 0 表示获取序号严格小于seq的最近五条消息

        同时检查 userId 是否有权访问 conversation_id。
    */
    static QueryHistoricalMessageResult queryHistoricalMessage(
        const std::uint64_t conversation_id, 
        const std::uint64_t seq,
        const std::uint64_t userId);
};
