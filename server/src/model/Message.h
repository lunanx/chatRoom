#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

class Message
{
private:
public:
    Message() = default;
    Message(
        const std::uint64_t &senderId,
        const std::string &senderName,
        const std::string &content,
        const std::uint64_t &conversationId,
        const std::uint64_t &serverMsgId,
        const std::uint64_t &clientMsgId,
        const std::uint64_t &seq,
        const std::uint16_t &type);

    const std::uint64_t &getSenderId() const;

    const std::string &getSenderName() const;

    const std::string &getContent() const;

    const std::uint64_t &getConversationId() const;

    const std::uint64_t &getServerMsgId() const;

    const std::uint64_t &getClientMsgId() const;

    const std::uint64_t &getSeq() const;

    const std::uint16_t &getType() const;

private:
    std::uint64_t m_senderId; // 表示谁发的,这个可由登录验证后直接得到

    std::string m_senderName; // 广播显示的名字,这个可由登录验证后直接得到

    std::string m_content; // 消息内容

    std::uint64_t m_conversationId; // 表示消息所属的会话窗口

    std::uint64_t m_serverMsgId; // 服务器生成的消息唯一ID

    std::uint64_t m_clientMsgId; // 客户端生成的消息唯一ID,用于识别重试消息、避免重复插入

    std::uint64_t m_seq; // 对应的Conversation内成功提交顺序分配的消息序号

    std::uint16_t m_type; // 消息类型，当前只有文本类型
};
