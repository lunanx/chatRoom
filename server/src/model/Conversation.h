#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <optional>

class Conversation
{
public:
    /*
        私聊构造函数
    */
    Conversation(
        std::uint16_t type,
        std::uint64_t userId1,
        std::uint64_t userId2);

    /*
        群聊构造函数
    */
    Conversation(
        std::uint16_t type,
        std::uint64_t groupId);

    void setId(std::uint64_t id);
    const std::optional<std::uint64_t> &getId() const;

    const std::uint16_t &getType() const;

    const std::optional<std::uint64_t> &getUserId1() const;

    const std::optional<std::uint64_t> &getUserId2() const;

    const std::optional<std::uint64_t> &getGroupId() const;

private:
    std::optional<std::uint64_t> m_id; //  数据库中会话唯一标识

    std::uint16_t m_type; // 会话类型 1:私聊,2:群聊

    std::optional<std::uint64_t> m_userId1; // 私聊用户1，必须小于2

    std::optional<std::uint64_t> m_userId2; // 私聊用户2

    std::optional<std::uint64_t> m_groupId; // 群聊群组id
};
