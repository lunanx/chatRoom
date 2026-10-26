#pragma once

#include <QString>
#include <QtGlobal>

// 本地消息在客户端的发送状态。
// Pending：已在本地显示，等待服务器确认。
// Sent：服务器已确认保存。
// Failed：发送失败或确认超时，可以使用原 clientMessageId 重试。
enum class MessageSendState
{
    Pending,
    Sent,
    Failed
};

// 客户端用于展示的一条文本消息。
// Pending 消息在服务器确认前可能还没有 senderId、serverMessageId 和 sequence。
struct ChatMessageRecord
{
    quint64 conversationId = 0;       // 数据库 Conversation.id
    quint64 serverMessageId = 0;      // 数据库 Message.id，用于消息去重
    QString clientMessageId;           // 客户端生成的 UUID，用于幂等重试和匹配确认
    quint64 senderId = 0;              // 数据库 User.id；本地待确认消息可能暂时为 0
    QString senderName;                // 发送者用户名
    QString content;                   // 文本内容
    quint64 sequence = 0;               // 会话内消息序号；本地待确认消息暂时为 0
    QString timeText;                  // 当前协议未返回创建时间时，使用客户端接收时间
    MessageSendState sendState = MessageSendState::Sent;
};
