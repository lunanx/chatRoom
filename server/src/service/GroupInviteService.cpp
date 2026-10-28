#include "GroupInviteService.h"

#include "db/DatabaseManager.h"
#include "protocol/ProtocolBody.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <utility>

namespace
{
    using protocolHeader::CommandType;

    CommandType setResponse(std::string &responseBody,
                            CommandType command,
                            const std::string &message,
                            nlohmann::json data = nullptr)
    {
        nlohmann::json response = {
            {protocolBody::CODE, static_cast<std::uint16_t>(command)},
            {protocolBody::MESSAGE, message}};
        if (!data.is_null())
        {
            response[protocolBody::DATA] = std::move(data);
        }
        responseBody = response.dump();
        return command;
    }

    GroupInviteResult fail(std::string &responseBody, const std::string &message)
    {
        setResponse(responseBody, CommandType::GROUP_MEMBER_INVITE_FAILED, message);
        return {CommandType::GROUP_MEMBER_INVITE_FAILED, 0, 0, {}};
    }

    bool isPositiveUnsignedInteger(const nlohmann::json &value)
    {
        if (value.is_number_unsigned())
        {
            return value.get<std::uint64_t>() > 0;
        }
        return value.is_number_integer() && value.get<std::int64_t>() > 0;
    }
}

GroupInviteResult GroupInviteService::inviteMembers(
    const std::string &body,
    std::string &responseBody,
    std::uint64_t authenticatedUserId)
{
    if (authenticatedUserId == 0)
    {
        return fail(responseBody, "当前登录用户无效，请重新登录");
    }

    nlohmann::json request;
    try
    {
        request = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        return fail(responseBody, "群聊邀请请求 JSON 格式不正确");
    }

    if (!request.is_object() ||
        !request.contains(protocolBody::CONVERSATION_ID) ||
        !isPositiveUnsignedInteger(request[protocolBody::CONVERSATION_ID]) ||
        !request.contains(protocolBody::USER_IDS) ||
        !request[protocolBody::USER_IDS].is_array())
    {
        return fail(responseBody, "请求必须包含有效的 conversation_id 和 user_ids 数组");
    }

    const std::uint64_t conversationId =
        request[protocolBody::CONVERSATION_ID].get<std::uint64_t>();
    const auto &requestedIds = request[protocolBody::USER_IDS];
    if (requestedIds.empty() || requestedIds.size() > 100)
    {
        return fail(responseBody, "每次邀请至少选择 1 位好友，最多选择 100 位");
    }

    std::vector<std::uint64_t> targetUserIds;
    std::unordered_set<std::uint64_t> uniqueIds;
    for (const auto &value : requestedIds)
    {
        if (!isPositiveUnsignedInteger(value))
        {
            return fail(responseBody, "被邀请用户 ID 必须是正整数");
        }
        const std::uint64_t userId = value.get<std::uint64_t>();
        if (userId == authenticatedUserId)
        {
            return fail(responseBody, "不能邀请自己加入群聊");
        }
        if (uniqueIds.insert(userId).second)
        {
            targetUserIds.push_back(userId);
        }
    }

    DatabaseManager db;
    if (!db.connect())
    {
        return fail(responseBody, "数据库连接失败，无法处理群聊邀请");
    }
    if (!db.beginTransaction())
    {
        return fail(responseBody, "服务器暂时无法开启邀请事务，请稍后重试");
    }

    const auto rollbackFailure = [&db, &responseBody](const std::string &message)
    {
        db.rollbackTransaction();
        return fail(responseBody, message);
    };

    // 只允许邀请真实的群聊会话，不能把私聊 conversation_id 当作群组使用。
    std::uint64_t groupId = 0;
    {
        auto groupQuery = db.query(
            "SELECT c.group_id FROM Conversation c "
            "INNER JOIN `Group` g ON g.id = c.group_id "
            "WHERE c.id = ? AND c.type = 2 AND g.status = 1 LIMIT 1",
            conversationId);
        if (!groupQuery.succeeded || !groupQuery.result)
        {
            return rollbackFailure("无法验证群聊，请稍后重试");
        }
        if (!groupQuery.result->next())
        {
            return rollbackFailure("群聊不存在、已停用或会话 ID 无效");
        }
        groupId = static_cast<std::uint64_t>(
            std::stoull(groupQuery.result->getString(1).c_str()));
    }

    // 邀请人必须是当前有效群成员。释放上一个查询结果后再执行下一条 SQL。
    {
        auto inviterQuery = db.query(
            "SELECT 1 FROM GroupMember WHERE group_id = ? AND user_id = ? LIMIT 1",
            groupId, authenticatedUserId);
        if (!inviterQuery.succeeded || !inviterQuery.result)
        {
            return rollbackFailure("无法验证当前群成员身份，请稍后重试");
        }
        if (!inviterQuery.result->next())
        {
            return rollbackFailure("只有当前群成员才能邀请好友加入群聊");
        }
    }

    std::vector<std::uint64_t> newlyInvited;
    for (const std::uint64_t targetId : targetUserIds)
    {
        // 目标账号必须有效。
        {
            auto userQuery = db.query(
                "SELECT 1 FROM `User` WHERE id = ? AND status = 1 LIMIT 1", targetId);
            if (!userQuery.succeeded || !userQuery.result)
            {
                return rollbackFailure("无法验证被邀请用户，请稍后重试");
            }
            if (!userQuery.result->next())
            {
                return rollbackFailure("被邀请用户不存在或账号不可用");
            }
        }

        // 本次邀请面向好友列表；服务器仍需验证双方确实存在有效好友关系。
        const std::uint64_t lowId = std::min(authenticatedUserId, targetId);
        const std::uint64_t highId = std::max(authenticatedUserId, targetId);
        {
            auto friendshipQuery = db.query(
                "SELECT 1 FROM Friendship WHERE user_id = ? AND friend_id = ? AND status = 1 LIMIT 1",
                lowId, highId);
            if (!friendshipQuery.succeeded || !friendshipQuery.result)
            {
                return rollbackFailure("无法验证好友关系，请稍后重试");
            }
            if (!friendshipQuery.result->next())
            {
                return rollbackFailure("只能邀请自己的有效好友加入群聊，请刷新好友列表后重试");
            }
        }

        bool alreadyMember = false;
        {
            auto memberQuery = db.query(
                "SELECT 1 FROM GroupMember WHERE group_id = ? AND user_id = ? LIMIT 1",
                groupId, targetId);
            if (!memberQuery.succeeded || !memberQuery.result)
            {
                return rollbackFailure("无法查询群成员列表，请稍后重试");
            }
            alreadyMember = memberQuery.result->next();
        }
        if (alreadyMember)
        {
            continue; // 已经在群里的好友跳过，不重复插入。
        }

        const int affectedRows = db.executeUpdate(
            "INSERT INTO GroupMember (group_id, user_id, role) VALUES (?, ?, 3)",
            groupId, targetId);
        if (affectedRows != 1)
        {
            return rollbackFailure("添加群成员失败，邀请未完成");
        }
        newlyInvited.push_back(targetId);
    }

    if (!db.commitTransaction())
    {
        db.rollbackTransaction();
        return fail(responseBody, "提交群聊邀请失败，请稍后重试");
    }

    nlohmann::json targetIds = nlohmann::json::array();
    for (const std::uint64_t userId : newlyInvited)
    {
        targetIds.push_back(userId);
    }
    nlohmann::json resultData = {
        {protocolBody::CONVERSATION_ID, conversationId},
        {protocolBody::GROUP_ID, groupId},
        {protocolBody::INVITED_COUNT, newlyInvited.size()},
        {protocolBody::USER_IDS, targetIds}};
    setResponse(responseBody, CommandType::GROUP_MEMBER_INVITE_SUCCESS,
                newlyInvited.empty() ? "所选好友已在群聊中，无需重复邀请"
                                     : "群聊邀请成功",
                std::move(resultData));
    return {CommandType::GROUP_MEMBER_INVITE_SUCCESS,
            conversationId,
            static_cast<std::uint64_t>(newlyInvited.size()),
            std::move(newlyInvited)};
}
