#pragma once

#include "protocol/ProtocolHeader.h"
#include <cstdint>
#include <string>
#include <vector>

struct GroupCreateResult
{
    protocolHeader::CommandType command{protocolHeader::CommandType::CREATE_GROUP_FAILED};
    // 新群聊对应的 Conversation.id；创建者的连接需要订阅它，才能收到之后的群消息。
    std::uint64_t conversationId{0};
    // 除群主外的初始成员；这些用户可能在线，需要接收会话刷新推送。
    std::vector<std::uint64_t> invitedUserIds;
};

class GroupCreateService
{
public:
    // authenticatedUserId 必须来自服务端的 ClientSession，不能信任请求体中的任何用户 ID。
    // 一次事务内完成：Group -> GroupMember(群主 + 初始成员) -> Conversation(type=2)。
    static GroupCreateResult createGroup(
        const std::string &body,
        std::string &responseBody,
        std::uint64_t authenticatedUserId);
};
