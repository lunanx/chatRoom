#include "MessageRepository.h"
#include "protocol/ProtocolBody.h"

#include <iostream>
#include <string>
#include <algorithm>

namespace
{
    std::uint64_t readUInt64(sql::ResultSet &result, int column)
    {
        // 将字符串转换成uint64_t
        return static_cast<std::uint64_t>(std::stoull(result.getString(column).c_str()));
    }

    struct ExistingMessageLookup
    {
        bool queryError{false};  // 数据库有问题或者sql语句错误
        bool found{false};       // 查询到相同了client_msg_id消息
        bool sameRequest{false}; // 如果是相同消息返回原来的 server_msg_id 和 seq，不重复广播;如果不是相同的，那就是被用于不同内容，拒绝请求
        std::optional<Message> savedMessage;
    };
    // 幂等查询，出现在client_msg_id存在时
    ExistingMessageLookup findExistingMessage(DatabaseManager &dbManager, const Message &requested, bool lockingRead = false)
    {
        ExistingMessageLookup lookup;
        std::string sql =
            "SELECT m.id, m.conversation_id, m.seq, m.type, m.content, c.type "
            "FROM Message m INNER JOIN Conversation c ON c.id = m.conversation_id "
            "WHERE m.sender_id = ? AND m.client_msg_id = ? LIMIT 1";
        if (lockingRead)
        {
            sql += " FOR UPDATE";
        }
        auto queryResult = dbManager.query(sql, requested.getSenderId(), requested.getClientMsgId());

        if (!queryResult.succeeded || !queryResult.result)
        {
            lookup.queryError = true;
            return lookup;
        }

        try
        {
            if (!queryResult.result->next())
            {
                return lookup;
            }

            lookup.found = true;
            const auto serverMsgId = readUInt64(*queryResult.result, 1);
            const auto conversationId = readUInt64(*queryResult.result, 2);
            const auto seq = readUInt64(*queryResult.result, 3);
            const auto type = static_cast<std::uint16_t>(readUInt64(*queryResult.result, 4));
            const auto contentSql = queryResult.result->getString(5);
            const std::string content(contentSql.c_str(), contentSql.length());
            const auto conversationType = static_cast<std::uint16_t>(readUInt64(*queryResult.result, 6));

            lookup.sameRequest =
                conversationId == requested.getConversationId() &&
                type == requested.getType() &&
                content == requested.getContent();

            lookup.savedMessage.emplace(
                requested.getSenderId(), requested.getSenderName(), content,
                conversationId, serverMsgId, requested.getClientMsgId(), seq,
                type, conversationType);
            return lookup;
        }
        catch (const std::exception &e)
        {
            std::cerr << "读取幂等消息记录失败: " << e.what() << '\n';
            lookup.queryError = true;
            return lookup;
        }
    }
}

CreateMessageResult MessageRepository::createMessage(const Message &message)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，无法保存消息\n";
        return {CreateMessageStatus::Failed};
    }
    if (!dbManager.beginTransaction())
    {
        std::cerr << "数据库开启事务失败，不允许保存消息\n";
        return {CreateMessageStatus::Failed};
    }

    try
    {
        // 先处理客户端重试：同一发送者、同一 client_msg_id 只能对应一条相同消息。
        auto existing = findExistingMessage(dbManager, message);
        if (existing.queryError)
        {
            dbManager.rollbackTransaction();
            return {CreateMessageStatus::Failed};
        }
        if (existing.found)
        {
            if (!existing.sameRequest)
            {
                dbManager.rollbackTransaction();
                return {CreateMessageStatus::ClientMsgIdConflict};
            }
            if (!dbManager.commitTransaction())
            {
                dbManager.rollbackTransaction();
                return {CreateMessageStatus::Failed};
            }
            return {CreateMessageStatus::Duplicate, std::move(existing.savedMessage)};
        }

        // 上面都不符合，说明是新的消息

        // 锁定目标会话的行。只有私聊参与者或正常群组的成员才能发送。
        // FOR UPDATE 让同一 Conversation 的消息序号依次处理。
        std::uint64_t lastSeq = 0;
        std::uint16_t conversationType = 0;
        {
            /*
            解释这个sql语句:
                数据库里查询一下这个会话是否存在(c.id = ?)、当前消息序号是多少、它是单聊还是群聊，
            (第一个AND 后面)同时确认发送消息的人有权在这个会话里发消息。-----是私聊的用户1或者2，是群聊的状态是正常，并且在群聊中没被禁用的。
            查到后给会话记录加锁，避免其他线程同时分配这个会话的消息序号。
            */
            auto conversationResult = dbManager.query(
                "SELECT c.last_seq, c.type FROM Conversation c "
                "WHERE c.id = ? AND ("
                " (c.type = 1 AND (c.user_id_1 = ? OR c.user_id_2 = ?)) "
                " OR (c.type = 2 AND EXISTS ("
                "   SELECT 1 FROM GroupMember gm INNER JOIN `Group` g ON g.id = gm.group_id "
                "   WHERE gm.group_id = c.group_id AND gm.user_id = ? AND g.status = 1 "
                "     AND (gm.muted_until IS NULL OR gm.muted_until <= CURRENT_TIMESTAMP(3))"
                " ))"
                ") FOR UPDATE",
                message.getConversationId(), message.getSenderId(), message.getSenderId(), message.getSenderId());

            if (!conversationResult.succeeded || !conversationResult.result)
            {
                dbManager.rollbackTransaction();
                return {CreateMessageStatus::Failed};
            }

            if (!conversationResult.result->next())
            {
                dbManager.rollbackTransaction();
                return {CreateMessageStatus::ConversationNotFoundOrForbidden};
            }

            lastSeq = readUInt64(*conversationResult.result, 1);
            conversationType = static_cast<std::uint16_t>(readUInt64(*conversationResult.result, 2));
        }
        if (lastSeq == std::numeric_limits<std::uint64_t>::max())
        {
            dbManager.rollbackTransaction();
            return {CreateMessageStatus::Failed};
        }
        const std::uint64_t nextSeq = lastSeq + 1;

        /*
        获取会话行锁后再次以当前读查重，普通SELECT，建立的快照看不到刚提交的重试消息。
        解释:
            两个相同的发送请求可能同时到达服务器。
            后到的请求先前查重时，可能因为数据库快照而看不到前一个请求尚未提交的消息。
            它等待前一个事务完成并拿到会话行锁后，再重新检查一次，避免把同一条消息插入两次。
            如果不获得锁会发生以下情况:
            时刻	T1	                T2
            t1  	开始事务
            t2  		                普通 SELECT，建立快照。也就是这次的findExistingMessage()
            t3  	插入 msg-001
            t4  	提交事务
            t5  		                普通 SELECT 仍可能看不到 msg-001,

            上述就会造成T1 后续提交的新消息可能不会出现在 T2 的普通查询结果里。
            因此这次的findExistingMessage()需要加锁,保证查询的是最新的
        */
        existing = findExistingMessage(dbManager, message, true);
        if (existing.queryError)
        {
            dbManager.rollbackTransaction();
            return {CreateMessageStatus::Failed, std::nullopt};
        }
        if (existing.found)
        {
            if (!existing.sameRequest)
            {
                dbManager.rollbackTransaction();
                return {CreateMessageStatus::ClientMsgIdConflict, std::nullopt};
            }
            if (!dbManager.commitTransaction())
            {
                dbManager.rollbackTransaction();
                return {CreateMessageStatus::Failed, std::nullopt};
            }
            return {CreateMessageStatus::Duplicate, std::move(existing.savedMessage)};
        }

        // Message.id 是数据库 AUTO_INCREMENT 主键，直接作为 server_msg_id 返回，
        // 防止使用 extern 全局计数器，因为如果使用extern全局计数器，重启服务器后又归零了
        const int insertedRows = dbManager.executeUpdate(
            "INSERT INTO Message (client_msg_id, conversation_id, sender_id, seq, type, content) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            message.getClientMsgId(), message.getConversationId(), message.getSenderId(),
            nextSeq, static_cast<int>(message.getType()), message.getContent());

        if (insertedRows != 1)
        {
            dbManager.rollbackTransaction();
            return {CreateMessageStatus::Failed, std::nullopt};
        }

        std::uint64_t serverMsgId = 0;
        {
            // LAST_INSERT_ID()获取当前数据库连接最近一次生成的自增 ID
            auto idResult = dbManager.query("SELECT LAST_INSERT_ID()");
            if (!idResult.succeeded || !idResult.result || !idResult.result->next())
            {
                dbManager.rollbackTransaction();
                return {CreateMessageStatus::Failed, std::nullopt};
            }
            serverMsgId = readUInt64(*idResult.result, 1);
        }

        // 由于当前事务持有 Conversation 行锁，last_seq 更新与消息插入保持一致。
        // 最近的一次findExistingMessage()设置了行锁
        const int updatedRows = dbManager.executeUpdate(
            "UPDATE Conversation SET last_seq = ? WHERE id = ?",
            nextSeq, message.getConversationId());
        if (updatedRows != 1)
        {
            dbManager.rollbackTransaction();
            return {CreateMessageStatus::Failed, std::nullopt};
        }

        if (!dbManager.commitTransaction())
        {
            dbManager.rollbackTransaction();
            return {CreateMessageStatus::Failed, std::nullopt};
        }
        // 返回一个完整的消息
        Message savedMessage(
            message.getSenderId(), message.getSenderName(), message.getContent(),
            message.getConversationId(), serverMsgId, message.getClientMsgId(),
            nextSeq, message.getType(), conversationType);
        return {CreateMessageStatus::Created, std::move(savedMessage)};
    }
    catch (const std::exception &e)
    {
        std::cerr << "保存消息时发生异常: " << e.what() << '\n';
        dbManager.rollbackTransaction();
        return {CreateMessageStatus::Failed, std::nullopt};
    }
    catch (...)
    {
        std::cerr << "保存消息时发生未知异常\n";
        dbManager.rollbackTransaction();
        return {CreateMessageStatus::Failed, std::nullopt};
    }
}

QueryHistoricalMessageResult MessageRepository::queryHistoricalMessage(const std::uint64_t conversation_id, const std::uint64_t cursorSeq, const std::uint64_t userId)
{
    DatabaseManager dbManager;
    if (!dbManager.connect())
    {
        std::cerr << "数据库连接失败，查询历史消息\n";
        return {QueryHistoricalMessageStatus::Error};
    }
    // 第一步：检查当前用户是否有权访问该会话。
    //
    // 私聊：当前用户必须是 user_id_1 或 user_id_2。
    // 群聊：当前用户必须是正常群组中的成员。

    auto permissionResult = dbManager.query(
        "SELECT c.id FROM Conversation c "
        "WHERE c.id = ? AND ("
        "  (c.type = 1 AND "
        "   (c.user_id_1 = ? OR c.user_id_2 = ?)) "
        "  OR (c.type = 2 AND EXISTS ("
        "    SELECT 1 FROM GroupMember gm "
        "    INNER JOIN `Group` g ON g.id = gm.group_id "
        "    WHERE gm.group_id = c.group_id "
        "      AND gm.user_id = ? "
        "      AND g.status = 1"
        "  ))"
        ") LIMIT 1",
        conversation_id,
        userId,
        userId,
        userId);

    if (!permissionResult.succeeded || !permissionResult.result)
    {
        std::cerr << "检查历史消息会话权限失败\n";
        return {QueryHistoricalMessageStatus::Error};
    }

    if (!permissionResult.result->next())
    {
        return {QueryHistoricalMessageStatus::Forbidden};
    }

    // 权限检查已经完成，不再需要第一次查询的结果。
    // 及时释放结果集和预处理语句，避免数据库资源长期占用，
    // 并降低未释放的查询资源，影响同一连接后续查询的风险。
    permissionResult.result.reset();
    permissionResult.statement.reset();

    // 第二步：查询历史消息。
    //
    // seq = 0：查询最新的 5 条消息。
    // seq > 0：查询序号严格小于 seq 的最近 5 条消息。
    //
    // 通过 User 表关联发送者信息。
    DatabaseQueryResult queryResult;

    if (cursorSeq == 0)
    {
        queryResult = dbManager.query(
            "SELECT m.seq, m.content, m.sender_id, u.username "
            "FROM Message m "
            "INNER JOIN `User` u ON u.id = m.sender_id "
            "WHERE m.conversation_id = ? "
            "  AND m.status = 1 "
            "ORDER BY m.seq DESC LIMIT 5",
            conversation_id);
    }
    else
    {
        queryResult = dbManager.query(
            "SELECT m.seq, m.content, m.sender_id,u.username "
            "FROM MESSAGE m "
            "INNER JOIN User u ON u.id = m.sender_id "
            "WHERE conversation_id = ? "
            "  AND status = 1 "
            "  AND seq < ? "
            "ORDER BY m.seq DESC LIMIT 5",
            conversation_id,
            cursorSeq);
    }

    if (!queryResult.succeeded || !queryResult.result)
    {
        std::cerr << "查询历史消息失败\n";
        return {QueryHistoricalMessageStatus::Error};
    }

    try
    {
        std::vector<HistoricalMessage> contents;

        while (queryResult.result->next())
        {
            HistoricalMessage message;

            // 第一列：消息序号
            message.seq = readUInt64(*queryResult.result, 1);

            // 第二列: 消息内容
            const auto textVal = queryResult.result->getString(2);
            message.text = std::string(textVal.c_str(), textVal.length());

            // 第三列:发送者ID
            message.senderId = readUInt64(*queryResult.result, 3);

            // 第四列:发送者用户名
            const auto nameVal = queryResult.result->getString(4);
            message.senderName = std::string(nameVal.c_str(), nameVal.length());

            contents.push_back(std::move(message));
        }

        if (contents.empty())
        {
            return {QueryHistoricalMessageStatus::NotFound};
        }
        // SQL 按 seq 倒序取最新的 5 条。
        // 返回客户端之前反转，让消息按 seq 从小到大排列。
        std::reverse(contents.begin(), contents.end());

        return {QueryHistoricalMessageStatus::Found, std::move(contents)};
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "查询历史消息失败: " << e.what()
                  << "，错误代码: " << e.getErrorCode() << '\n';
        return {QueryHistoricalMessageStatus::Error};
    }
    catch (const std::exception &e)
    {
        // 例如读取数据库数值并转换成 uint64_t 时出现异常。
        std::cerr << "处理历史消息结果时发生异常: "
                  << e.what() << '\n';

        return {QueryHistoricalMessageStatus::Error};
    }
}
