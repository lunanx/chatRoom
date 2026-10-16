#include "Message.h"

Message::Message(
    const std::uint64_t &senderId, 
    const std::string &senderName, 
    const std::string &content,
    const std::uint64_t &conversationId,
    const std::uint64_t &serverMsgId,
    const std::uint64_t &clientMsgId,
    const std::uint64_t &seq,
    const std::uint16_t &type):
    m_senderId(senderId),
    m_senderName(senderName),
    m_content(content),
    m_conversationId(conversationId),
    m_serverMsgId(serverMsgId),
    m_clientMsgId(clientMsgId),
    m_seq(seq),
    m_type(type)
{
}

const std::uint64_t &Message::getSenderId() const
{
    return m_senderId;
}

const std::string &Message::getSenderName() const
{
    return m_senderName;
}

const std::string &Message::getContent() const
{
    return m_content;
}

const std::uint64_t &Message::getConversationId() const
{
    return m_conversationId;
}

const std::uint64_t &Message::getServerMsgId() const
{
    return m_serverMsgId;
}

const std::uint64_t &Message::getClientMsgId() const
{
    return m_clientMsgId;
}

const std::uint64_t &Message::getSeq() const
{
    return m_seq;
}

const std::uint16_t &Message::getType() const
{
    return m_type;
}
