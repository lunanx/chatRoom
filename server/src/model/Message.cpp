#include "Message.h"

Message::Message(
    const std::uint64_t &senderId, 
    const std::string &senderName, 
    const std::string &content,
    const std::uint64_t &conversationId):
    m_senderId(senderId),
    m_senderName(senderName),
    m_content(content),
    m_conversationId(conversationId)
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
