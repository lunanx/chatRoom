#pragma once

#include <cstdint>
#include <string>

/*
    Message 表示一条聊天消息。
    serverMsgId 和 seq 在数据库成功保存后才有真实值；
    本地待保存对象中它们暂时为 0。
*/
class Message
{
public:
    Message() = default;

    // 创建待保存消息：身份来自 ClientSession，请求字段来自客户端。
    Message(
        std::uint64_t senderId,
        std::string senderName,
        std::string content,
        std::uint64_t conversationId,
        std::string clientMsgId,
        std::uint16_t type);

    // 创建已保存消息。serverMsgId 对应数据库自动生成的 Message.id。
    Message(
        std::uint64_t senderId,
        std::string senderName,
        std::string content,
        std::uint64_t conversationId,
        std::uint64_t serverMsgId,
        std::string clientMsgId,
        std::uint64_t seq,
        std::uint16_t type,
        std::uint16_t conversationType);

    std::uint64_t getSenderId() const;

    const std::string &getSenderName() const;

    const std::string &getContent() const;

    std::uint64_t getConversationId() const;

    std::uint64_t getServerMsgId() const;

    const std::string &getClientMsgId() const;

    std::uint64_t getSeq() const;

    std::uint16_t getType() const;

    std::uint16_t getConversationType() const;

private:
    std::uint64_t m_senderId{0}; // 表示谁发的,这个可由登录验证后直接得到

    std::string m_senderName; // 广播显示的名字,这个可由登录验证后直接得到

    std::string m_content; // 消息内容

    std::uint64_t m_conversationId{0}; // 表示消息所属的会话窗口

    std::uint64_t m_serverMsgId{0}; // 服务器生成的消息唯一ID,就是数据库里的 Message.id

    std::string m_clientMsgId; // 客户端生成的消息唯一ID,用于识别重试消息、避免重复插入

    std::uint64_t m_seq{0}; // 对应的Conversation内成功提交顺序分配的消息序号

    std::uint16_t m_type{1}; // 消息类型，当前只有文本类型
    
    std::uint16_t m_conversationType{0};// 来自 Conversation.type
};
