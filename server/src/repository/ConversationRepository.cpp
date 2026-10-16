#include "ConversationRepository.h"

#include <iostream>
#include <string>

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