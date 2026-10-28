#pragma once

#include "protocol/ProtocolHeader.h"
#include <cstdint>
#include <string>
#include <vector>

struct GroupInviteResult
{
    protocolHeader::CommandType command{protocolHeader::CommandType::GROUP_MEMBER_INVITE_FAILED};
    std::uint64_t conversationId{0};
    std::uint64_t invitedCount{0};
    // 只包含本次实际新增的成员；这些用户可能在线，需要接收会话刷新推送。
    std::vector<std::uint64_t> invitedUserIds;
};

class GroupInviteService
{
public:
    // authenticatedUserId 必须来自服务端的 ClientSession，不能信任请求体中的邀请人 ID。
    static GroupInviteResult inviteMembers(
        const std::string &body,
        std::string &responseBody,
        std::uint64_t authenticatedUserId);
};
