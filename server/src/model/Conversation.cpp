#include "Conversation.h"

Conversation::Conversation( 
    std::uint16_t type, 
    std::uint64_t userId1, 
    std::uint64_t userId2):
    m_type(type),
    m_userId1(userId1),
    m_userId2(userId2)
{
}

Conversation::Conversation(
    std::uint16_t type, 
    std::uint64_t groupId):
    m_type(type),
    m_groupId(groupId)
{
}

void Conversation::setId(std::uint64_t id)
{
    m_id = id;
}

const std::optional<std::uint64_t> &Conversation::getId() const
{
    return m_id;
}

const std::uint16_t& Conversation::getType() const
{
    return m_type;
}

const std::optional<std::uint64_t>& Conversation::getUserId1() const
{
    return m_userId1;
}

const std::optional<std::uint64_t>& Conversation::getUserId2() const
{
    return m_userId2;
}

const std::optional<std::uint64_t>& Conversation::getGroupId() const
{
    return m_groupId;
}

