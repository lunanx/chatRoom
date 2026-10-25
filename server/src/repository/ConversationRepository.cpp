#include "ConversationRepository.h"

#include <iostream>
#include <string>
#include <vector>
#include <utility>

namespace
{
    std::uint64_t readUInt64(sql::ResultSet &result, int column)
    {
        // 将字符串转换成uint64_t
        return static_cast<std::uint64_t>(std::stoull(result.getString(column).c_str()));
    }
}

CreateConversationResult ConversationRepository::createPrivateConversation(const Conversation &conversation)
{

    if (!conversation.getUserId1().has_value() || !conversation.getUserId2().has_value())
    {
        return CreateConversationResult::Failed;
    }

    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法创建私聊\n";
        return CreateConversationResult::Failed;
    }

    const int affectedRows = dbManager.executeUpdate(
        "INSERT INTO Conversation (type,user_id_1,user_id_2) VALUES (?,?,?)",
        conversation.getType(),
        conversation.getUserId1().value(),
        conversation.getUserId2().value());

    return affectedRows == 1 ? CreateConversationResult::Created : CreateConversationResult::Failed;
}

CreateConversationResult ConversationRepository::createGroupConversation(const Conversation &conversation)
{
    if (!conversation.getGroupId().has_value())
    {
        return CreateConversationResult::Failed;
    }

    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法创建群聊\n";
        return CreateConversationResult::Failed;
    }

    const int affectedRows = dbManager.executeUpdate(
        "INSERT INTO Conversation (type,group_id) VALUES (?,?)",
        conversation.getType(),
        conversation.getGroupId().value());

    return affectedRows == 1 ? CreateConversationResult::Created : CreateConversationResult::Failed;
}

getLastSeqResult ConversationRepository::getLastSeqByConversationId(std::uint64_t conversationId)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询会话 last_seq\n";
        return {ConversationQueryStatus::Error, 0};
    }

    auto queryResult = dbManager.query(
        "SELECT last_seq FROM Conversation WHERE id = ?",
        conversationId);

    if (!queryResult.succeeded || !queryResult.result)
    {
        return {ConversationQueryStatus::Error, 0};
    }

    try
    {
        if (!queryResult.result->next())
        {
            return {ConversationQueryStatus::NotFound, 0};
        }
        return {ConversationQueryStatus::Found, readUInt64(*queryResult.result, 1)};
    }
    catch (const std::exception &e)
    {
        std::cerr << "读取会话 last_seq 失败: " << e.what() << '\n';
        return {ConversationQueryStatus::Error, 0};
    }
}

getConversationIdResult ConversationRepository::getConversationIdByConversationObject(const Conversation &conversation)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询 conversation_id\n";
        return {ConversationQueryStatus::Error, 0};
    }

    DatabaseQueryResult queryResult;
    if (conversation.getType() == 1 && conversation.getUserId1().has_value() && conversation.getUserId2().has_value())
    {
        queryResult = dbManager.query(
            "SELECT id FROM Conversation WHERE type = 1 AND user_id_1 = ? AND user_id_2 = ? LIMIT 1",
            conversation.getUserId1().value(), conversation.getUserId2().value());
    }
    else if (conversation.getType() == 2 && conversation.getGroupId().has_value())
    {
        queryResult = dbManager.query(
            "SELECT id FROM Conversation WHERE type = 2 AND group_id = ? LIMIT 1",
            conversation.getGroupId().value());
    }
    else
    {
        return {ConversationQueryStatus::Error, 0};
    }

    if (!queryResult.succeeded || !queryResult.result)
    {
        return {ConversationQueryStatus::Error, 0};
    }

    try
    {
        if (!queryResult.result->next())
        {
            return {ConversationQueryStatus::NotFound, 0};
        }
        return {ConversationQueryStatus::Found, readUInt64(*queryResult.result, 1)};
    }
    catch (const std::exception &e)
    {
        std::cerr << "查询 conversation_id 失败: " << e.what() << '\n';
        return {ConversationQueryStatus::Error, 0};
    }
}

ConversationQueryStatus ConversationRepository::checkActiveGroupMember(std::uint64_t groupId, std::uint64_t userId)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询 check group member active status\n";
        return ConversationQueryStatus::Error;
    }

    auto queryResult = dbManager.query(
        "SELECT 1 FROM `Group` g "
        "INNER JOIN GroupMember gm ON gm.group_id = g.id "
        "WHERE g.id = ? AND g.status = 1 AND gm.user_id = ? LIMIT 1",
        groupId, userId);

    if (!queryResult.succeeded || !queryResult.result)
    {
        return ConversationQueryStatus::Error;
    }

    try
    {
        return queryResult.result->next() ? ConversationQueryStatus::Found : ConversationQueryStatus::NotFound;
    }
    catch (const std::exception &e)
    {
        std::cerr << "检查群组成员关系失败: " << e.what() << '\n';
        return ConversationQueryStatus::Error;
    }
}

ConversationQueryStatus ConversationRepository::getConversationsForUser(
    std::uint64_t userId,
    std::vector<ConversationListEntry> &conversations)
{
    conversations.clear();
    if (userId == 0)
    {
        return ConversationQueryStatus::NotFound;
    }

    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法查询会话列表\n";
        return ConversationQueryStatus::Error;
    }

    // 私聊名称显示对方昵称，群聊名称显示群名；没有消息的会话也会被列出。
    // 查询依据是当前已认证用户 ID，不接受客户端自行声明查询谁的会话。
    auto queryResult = dbManager.query(
        "SELECT c.id, c.type, c.last_seq, "
        "CASE WHEN c.type = 1 THEN "
        "  CASE WHEN c.user_id_1 = ? THEN u2.nickname ELSE u1.nickname END "
        "  ELSE g.name END AS display_name, "
        "COALESCE(CASE WHEN c.type = 1 THEN "
        "  CASE WHEN c.user_id_1 = ? THEN u2.avatar ELSE u1.avatar END "
        "  ELSE g.avatar END, '') AS avatar, "
        "CASE WHEN c.type = 1 THEN "
        "  CASE WHEN c.user_id_1 = ? THEN u2.id ELSE u1.id END "
        "  ELSE 0 END AS target_user_id, "
        "CASE WHEN c.type = 1 THEN "
        "  CASE WHEN c.user_id_1 = ? THEN u2.username ELSE u1.username END "
        "  ELSE '' END AS target_username, "
        "COALESCE(lm.content, '') AS last_message, "
        "DATE_FORMAT(COALESCE(lm.created_at, c.updated_at, c.created_at), '%Y-%m-%d %H:%i:%s') AS last_time "
        "FROM Conversation c "
        "LEFT JOIN `User` u1 ON u1.id = c.user_id_1 "
        "LEFT JOIN `User` u2 ON u2.id = c.user_id_2 "
        "LEFT JOIN `Group` g ON g.id = c.group_id "
        "LEFT JOIN GroupMember gm ON gm.group_id = c.group_id AND gm.user_id = ? "
        "LEFT JOIN Message lm ON lm.conversation_id = c.id "
        "  AND lm.seq = c.last_seq AND lm.status = 1 "
        "WHERE ((c.type = 1 AND (c.user_id_1 = ? OR c.user_id_2 = ?) "
        "         AND u1.status = 1 AND u2.status = 1) "
        "    OR (c.type = 2 AND g.status = 1 AND gm.user_id IS NOT NULL)) "
        "ORDER BY COALESCE(lm.created_at, c.updated_at, c.created_at) DESC, c.id DESC",
        userId, userId, userId, userId, userId, userId, userId);

    if (!queryResult.succeeded || !queryResult.result)
    {
        return ConversationQueryStatus::Error;
    }

    try
    {
        while (queryResult.result->next())
        {
            ConversationListEntry entry;
            entry.conversationId = readUInt64(*queryResult.result, 1);
            entry.type = static_cast<std::uint16_t>(readUInt64(*queryResult.result, 2));
            entry.lastSeq = readUInt64(*queryResult.result, 3);
            entry.displayName = queryResult.result->getString(4).c_str();
            entry.avatar = queryResult.result->getString(5).c_str();
            entry.targetUserId = readUInt64(*queryResult.result, 6);
            entry.targetUsername = queryResult.result->getString(7).c_str();
            entry.lastMessage = queryResult.result->getString(8).c_str();
            entry.lastTime = queryResult.result->getString(9).c_str();
            conversations.push_back(std::move(entry));
        }
        return ConversationQueryStatus::Found;
    }
    catch (const std::exception &e)
    {
        std::cerr << "读取会话列表失败: " << e.what() << '\n';
        conversations.clear();
        return ConversationQueryStatus::Error;
    }
}

