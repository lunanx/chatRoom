#include "GroupCreateService.h"

#include "db/DatabaseManager.h"
#include "protocol/ProtocolBody.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <utility>

namespace
{
    using protocolHeader::CommandType;

    // 群名称按 Unicode 字符计数；数据库 Group.name 为 VARCHAR(128)，这里留出余量。
    constexpr int kMaxGroupNameCharacters = 50;
    // 初始成员（不含群主）数量上限，与群邀请的单次上限一致。
    constexpr std::size_t kMaxInitialMembers = 100;

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

    GroupCreateResult fail(std::string &responseBody, const std::string &message)
    {
        setResponse(responseBody, CommandType::CREATE_GROUP_FAILED, message);
        return {CommandType::CREATE_GROUP_FAILED, 0, {}};
    }

    bool isPositiveUnsignedInteger(const nlohmann::json &value)
    {
        if (value.is_number_unsigned())
        {
            return value.get<std::uint64_t>() > 0;
        }
        return value.is_number_integer() && value.get<std::int64_t>() > 0;
    }

    std::string trimAsciiWhitespace(const std::string &text)
    {
        const auto isSpace = [](unsigned char c)
        { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && isSpace(static_cast<unsigned char>(text[begin])))
        {
            ++begin;
        }
        while (end > begin && isSpace(static_cast<unsigned char>(text[end - 1])))
        {
            --end;
        }
        return text.substr(begin, end - begin);
    }

    // 返回 UTF-8 字符数；不是合法 UTF-8 时返回 -1。
    int countUtf8Characters(const std::string &text)
    {
        std::size_t i = 0;
        int count = 0;
        while (i < text.size())
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            std::size_t length = 0;
            if (c < 0x80)
            {
                length = 1;
            }
            else if (c >= 0xC2 && c <= 0xDF)
            {
                length = 2;
            }
            else if ((c & 0xF0) == 0xE0)
            {
                length = 3;
            }
            else if (c >= 0xF0 && c <= 0xF4)
            {
                length = 4;
            }
            else
            {
                return -1;
            }
            if (i + length > text.size())
            {
                return -1;
            }
            for (std::size_t k = 1; k < length; ++k)
            {
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                {
                    return -1;
                }
            }
            i += length;
            ++count;
        }
        return count;
    }

    bool hasControlCharacter(const std::string &text)
    {
        return std::any_of(text.begin(), text.end(), [](unsigned char c)
                           { return c < 0x20 || c == 0x7F; });
    }

    // 同一连接上紧跟在 INSERT 之后调用，读取该连接刚生成的自增 ID。
    bool fetchLastInsertId(DatabaseManager &db, std::uint64_t &id)
    {
        auto query = db.query("SELECT LAST_INSERT_ID()");
        if (!query.succeeded || !query.result || !query.result->next())
        {
            return false;
        }
        id = static_cast<std::uint64_t>(std::stoull(query.result->getString(1).c_str()));
        return id > 0;
    }
}

GroupCreateResult GroupCreateService::createGroup(
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
        return fail(responseBody, "创建群聊请求 JSON 格式不正确");
    }

    if (!request.is_object() ||
        !request.contains(protocolBody::GROUP_NAME) ||
        !request[protocolBody::GROUP_NAME].is_string() ||
        !request.contains(protocolBody::USER_IDS) ||
        !request[protocolBody::USER_IDS].is_array())
    {
        return fail(responseBody, "请求必须包含 group_name 字符串和 user_ids 数组");
    }

    const std::string groupName =
        trimAsciiWhitespace(request[protocolBody::GROUP_NAME].get<std::string>());
    const int nameCharacters = countUtf8Characters(groupName);
    if (nameCharacters < 0)
    {
        return fail(responseBody, "群聊名称不是有效的 UTF-8 文本");
    }
    if (nameCharacters == 0)
    {
        return fail(responseBody, "群聊名称不能为空");
    }
    if (nameCharacters > kMaxGroupNameCharacters)
    {
        return fail(responseBody, "群聊名称不能超过 50 个字符");
    }
    if (hasControlCharacter(groupName))
    {
        return fail(responseBody, "群聊名称不能包含控制字符");
    }

    const auto &requestedIds = request[protocolBody::USER_IDS];
    if (requestedIds.empty() || requestedIds.size() > kMaxInitialMembers)
    {
        return fail(responseBody, "创建群聊时请选择 1 到 100 位好友");
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
        return fail(responseBody, "数据库连接失败，无法创建群聊");
    }
    if (!db.beginTransaction())
    {
        return fail(responseBody, "服务器暂时无法开启创建群聊事务，请稍后重试");
    }

    const auto rollbackFailure = [&db, &responseBody](const std::string &message)
    {
        db.rollbackTransaction();
        return fail(responseBody, message);
    };

    // 群主必须是有效账号；Group.owner_id 外键也会兜底，但这里给出可读的错误信息。
    {
        auto ownerQuery = db.query(
            "SELECT 1 FROM `User` WHERE id = ? AND status = 1 LIMIT 1", authenticatedUserId);
        if (!ownerQuery.succeeded || !ownerQuery.result)
        {
            return rollbackFailure("无法验证当前登录用户，请稍后重试");
        }
        if (!ownerQuery.result->next())
        {
            return rollbackFailure("当前登录账号不可用，请重新登录");
        }
    }

    // 初始成员必须是群主自己的有效好友；好友关系由服务端核实，不信任客户端。
    for (const std::uint64_t targetId : targetUserIds)
    {
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
    }

    // 1) 群本体
    if (db.executeUpdate("INSERT INTO `Group` (name, owner_id) VALUES (?, ?)",
                         groupName, authenticatedUserId) != 1)
    {
        return rollbackFailure("创建群聊失败，请稍后重试");
    }
    std::uint64_t groupId = 0;
    if (!fetchLastInsertId(db, groupId))
    {
        return rollbackFailure("创建群聊失败，无法获取群 ID");
    }

    // 2) 群主（role=1）与初始成员（role=3）
    if (db.executeUpdate("INSERT INTO GroupMember (group_id, user_id, role) VALUES (?, ?, 1)",
                         groupId, authenticatedUserId) != 1)
    {
        return rollbackFailure("添加群主失败，群聊未创建");
    }
    for (const std::uint64_t targetId : targetUserIds)
    {
        if (db.executeUpdate("INSERT INTO GroupMember (group_id, user_id, role) VALUES (?, ?, 3)",
                             groupId, targetId) != 1)
        {
            return rollbackFailure("添加群成员失败，群聊未创建");
        }
    }

    // 3) 群聊会话（type=2）；之后发消息、拉历史、会话列表都以 Conversation.id 为准。
    if (db.executeUpdate("INSERT INTO Conversation (type, group_id) VALUES (2, ?)", groupId) != 1)
    {
        return rollbackFailure("创建群聊会话失败，群聊未创建");
    }
    std::uint64_t conversationId = 0;
    if (!fetchLastInsertId(db, conversationId))
    {
        return rollbackFailure("创建群聊会话失败，无法获取会话 ID");
    }

    if (!db.commitTransaction())
    {
        db.rollbackTransaction();
        return fail(responseBody, "提交创建群聊失败，请稍后重试");
    }

    nlohmann::json memberIds = nlohmann::json::array();
    for (const std::uint64_t userId : targetUserIds)
    {
        memberIds.push_back(userId);
    }
    nlohmann::json resultData = {
        {protocolBody::CONVERSATION_ID, conversationId},
        {protocolBody::GROUP_ID, groupId},
        {protocolBody::GROUP_NAME, groupName},
        {protocolBody::MEMBER_COUNT, targetUserIds.size() + 1},
        {protocolBody::USER_IDS, memberIds}};
    setResponse(responseBody, CommandType::CREATE_GROUP_SUCCESS, "群聊创建成功",
                std::move(resultData));
    return {CommandType::CREATE_GROUP_SUCCESS, conversationId, std::move(targetUserIds)};
}
