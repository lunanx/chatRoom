#pragma once

#include <QString>
#include <QtGlobal>

// 客户端用于展示的一条已被服务器保存的文本消息。
// 字段与服务端 GROUP_MESSAGE_RECV / PRIVATE_MESSAGE_RECV 推送中的 JSON 对应。
struct ChatMessageRecord
{
    quint64 conversationId = 0;   // 数据库 Conversation.id
    quint64 serverMessageId = 0; // 数据库 Message.id，用于消息去重
    QString clientMessageId;     // 客户端生成的 UUID，用于识别本端发送的消息
    quint64 senderId = 0;        // 数据库 User.id
    QString senderName;          // 服务端从已认证的 ClientSession 获取的用户名
    QString content;             // 文本内容
    quint64 sequence = 0;         // 会话内单调递增的消息序号
    QString timeText;             // 当前协议未返回创建时间，暂时显示接收时间
};
