#include "ConversationRepository.h"

CreateConversationResult ConversationRepository::createPrivateConversation(const Conversation &conversation)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法创建私聊\n";
        return CreateConversationResult::Failed;
    }

    const int affectedRows = dbManager.executeUpdate(
        "INSERT INTO Conversation (type,user_id_1,user_id_2) VALUES (?,?,?)",
        conversation.getType(),
        conversation.getUserId1(),
        conversation.getUserId2());

    return affectedRows == 1 ? CreateConversationResult::Created : CreateConversationResult::Failed;
}

CreateConversationResult ConversationRepository::createGroupConversation(const Conversation &conversation)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法创建群聊\n";
        return CreateConversationResult::Failed;
    }

    const int affectedRows = dbManager.executeUpdate(
        "INSERT INTO Conversation (type,group_id) VALUES (?,?)",
        conversation.getType(),
        conversation.getGroupId());

    return affectedRows == 1 ? CreateConversationResult::Created : CreateConversationResult::Failed;
}

bool ConversationRepository::updateLastSeqByConversationId(const std::uint64_t &conversation_id, const std::uint64_t &seq)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败,无法更新当前会话的last_seq\n";
        return false;
    }
    // seq一定比last_seq大，上层调用时会先处理
    return dbManager.executeUpdate(
               "UPDATE Conversation SET last_seq = ? WHERE id = ?", seq, conversation_id) > 0;
}

getLastSeqResult ConversationRepository::getLastSeqByConversationId(const std::uint64_t &conversation_id, const std::uint64_t &seq)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败,无法查询last_id\n";
        return {ConversationQueryStatus::Error, static_cast<std::uint64_t>(-1)};
    }

    auto queryResult = dbManager.query(
        "SELECT last_seq FROM Conversation WHERE id = ?",
        conversation_id);

    if (!queryResult.succeeded || !queryResult.result)
    {
        return {ConversationQueryStatus::Error, static_cast<std::uint64_t>(-1)};
    }

    try
    {
        if (!queryResult.result->next())
        {
            return {ConversationQueryStatus::NotFound, static_cast<std::uint64_t>(-1)};
        }
        std::uint64_t lastSeq = queryResult.result->getInt64(protocolHeader::LAST_SEQ);
        return {ConversationQueryStatus::Found, lastSeq};
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "查询最新会话id失败: " << e.what()
                  << "，错误代码: " << e.getErrorCode() << '\n';
        return {ConversationQueryStatus::Error, static_cast<std::uint64_t>(-1)};
    }
}

getConversationIdResult ConversationRepository::getConversationIdByConversationObject(const Conversation &conversation)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败,无法查询last_id\n";
        return {ConversationQueryStatus::Error, static_cast<std::uint64_t>(-1)};
    }
    DatabaseQueryResult queryResult;
    if (conversation.getType() == 1)
    {
        queryResult = dbManager.query(
            "SELECT id  FROM Conversation WHERE type = 1 AND user_id_1 = ? AND user_id_2 = ?", conversation.getUserId1(), conversation.getUserId2());
    }
    else
    {
        queryResult = dbManager.query(
            "SELECT id  FROM Conversation WHERE type = 2 AND group_id = ?", conversation.getGroupId());
    }

    if (!queryResult.succeeded || !queryResult.result)
    {
        return {ConversationQueryStatus::Error, static_cast<std::uint64_t>(-1)};
    }

    try
    {
        if (!queryResult.result->next())
        {
            return {ConversationQueryStatus::NotFound, static_cast<std::uint64_t>(-1)};
        }
        std::uint64_t conversation_id = queryResult.result->getInt64(protocolHeader::ID);
        return {ConversationQueryStatus::Found, conversation_id};
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "查询会话id失败: " << e.what()
                  << "，错误代码: " << e.getErrorCode() << '\n';
        return {ConversationQueryStatus::Error, static_cast<std::uint64_t>(-1)};
    }

}
