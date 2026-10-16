#include "Message.h"

#include <utility>

Message::Message(
    std::uint64_t senderId,
    std::string senderName,
    std::string content,
    std::uint64_t conversationId,
    std::string clientMsgId,
    std::uint16_t type)
    : m_senderId(senderId),
      m_senderName(std::move(senderName)),
      m_content(std::move(content)),
      m_conversationId(conversationId),
      m_clientMsgId(std::move(clientMsgId)),
      m_type(type)
{
}

Message::Message(
    std::uint64_t senderId,
    std::string senderName,
    std::string content,
    std::uint64_t conversationId,
    std::uint64_t serverMsgId,
    std::string clientMsgId,
    std::uint64_t seq,
    std::uint16_t type,
    std::uint16_t conversationType)
    : m_senderId(senderId),
      m_senderName(std::move(senderName)),
      m_content(std::move(content)),
      m_conversationId(conversationId),
      m_serverMsgId(serverMsgId),
      m_clientMsgId(std::move(clientMsgId)),
      m_seq(seq),
      m_type(type),
      m_conversationType(conversationType)
{
}

std::uint64_t Message::getSenderId() const
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
std::uint64_t Message::getConversationId() const
{
    return m_conversationId;
}
std::uint64_t Message::getServerMsgId() const
{
    return m_serverMsgId;
}
const std::string &Message::getClientMsgId() const
{
    return m_clientMsgId;
}
std::uint64_t Message::getSeq() const
{
    return m_seq;
}
std::uint16_t Message::getType() const
{
    return m_type;
}
std::uint16_t Message::getConversationType() const
{
    return m_conversationType;
}
