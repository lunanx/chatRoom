#include "FriendRepository.h"

#include <algorithm>
#include <iostream>
#include <utility>

namespace
{
    constexpr int DUPLICATE_ENTRY_ERROR = 1062;

    std::string sqlStringToStdString(const sql::SQLString &value)
    {
        return std::string(value.c_str(), value.length());
    }

    // 调用者必须已开启事务。使用固定的 ID 顺序锁定用户行，降低相反方向申请造成死锁的概率。
    // 返回 1=账号正常，0=账号不存在/不可用，-1=数据库错误。
    int lockActiveUser(DatabaseManager &db, std::uint64_t userId)
    {
        auto result = db.query(
            "SELECT status, deleted_at FROM User WHERE id = ? FOR UPDATE", userId);
        if (!result.succeeded || !result.result)
        {
            return -1;
        }

        try
        {
            if (!result.result->next())
            {
                return 0;
            }
            const auto status = result.result->getUInt("status");
            const bool deletedAtIsNull = result.result->isNull("deleted_at");
            return status == 1 && deletedAtIsNull ? 1 : 0;
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "检查/锁定好友业务用户状态失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            return -1;
        }
    }

    int lockTwoActiveUsers(DatabaseManager &db, std::uint64_t userId1, std::uint64_t userId2)
    {
        const auto low = std::min(userId1, userId2);
        const auto high = std::max(userId1, userId2);
        const int first = lockActiveUser(db, low);
        if (first != 1)
        {
            return first;
        }
        const int second = lockActiveUser(db, high);
        if (second != 1)
        {
            return second;
        }
        return 1;
    }

    bool queryHasRow(DatabaseManager &db, const std::string &sqlText,
                     std::uint64_t first, std::uint64_t second, bool &hasRow)
    {
        auto result = db.query(sqlText, first, second);
        if (!result.succeeded || !result.result)
        {
            return false;
        }
        try
        {
            hasRow = result.result->next();
            return true;
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "查询好友关系/申请状态失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            return false;
        }
    }

    bool readLastInsertId(DatabaseManager &db, std::uint64_t &requestId)
    {
        auto result = db.query("SELECT LAST_INSERT_ID() AS id");
        if (!result.succeeded || !result.result)
        {
            return false;
        }
        try
        {
            if (!result.result->next())
            {
                return false;
            }
            requestId = result.result->getUInt64("id");
            return requestId != 0;
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "读取好友申请 ID 失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            return false;
        }
    }
}

FriendOperationStatus FriendRepository::sendRequest(
    std::uint64_t senderId,
    const std::string &targetUsername,
    std::uint64_t &requestId)
{
    requestId = 0;
    DatabaseManager db;
    if (!db.connect())
    {
        std::cerr << "数据库连接失败，无法发送好友申请\n";
        return FriendOperationStatus::Error;
    }

    std::uint64_t receiverId = 0;
    {
        auto userResult = db.query(
            "SELECT id FROM User WHERE username = ? AND status = 1 AND deleted_at IS NULL LIMIT 1",
            targetUsername);
        if (!userResult.succeeded || !userResult.result)
        {
            return FriendOperationStatus::Error;
        }
        try
        {
            if (!userResult.result->next())
            {
                return FriendOperationStatus::UserNotFound;
            }
            receiverId = userResult.result->getUInt64("id");
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "查询好友申请目标用户失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            return FriendOperationStatus::Error;
        }
    }

    if (senderId == 0 || receiverId == 0)
    {
        return FriendOperationStatus::UserUnavailable;
    }
    if (senderId == receiverId)
    {
        return FriendOperationStatus::SelfRequest;
    }

    if (!db.beginTransaction())
    {
        return FriendOperationStatus::Error;
    }

    const int usersStatus = lockTwoActiveUsers(db, senderId, receiverId);
    if (usersStatus != 1)
    {
        db.rollbackTransaction();
        return usersStatus == 0 ? FriendOperationStatus::UserUnavailable
                                : FriendOperationStatus::Error;
    }

    const auto low = std::min(senderId, receiverId);
    const auto high = std::max(senderId, receiverId);
    bool exists = false;
    if (!queryHasRow(db,
                     "SELECT 1 FROM Friendship WHERE user_id = ? AND friend_id = ? AND status = 1 LIMIT 1",
                     low, high, exists))
    {
        db.rollbackTransaction();
        return FriendOperationStatus::Error;
    }
    if (exists)
    {
        db.rollbackTransaction();
        return FriendOperationStatus::AlreadyFriends;
    }

    // 先检查发往同一用户对的任意方向待处理请求，避免同时出现 A->B 和 B->A 两条申请。
    FriendOperationStatus pendingStatus = FriendOperationStatus::Success;
    {
        auto pendingResult = db.query(
            "SELECT sender_id, receiver_id FROM FriendRequest "
            "WHERE status = 0 AND ((sender_id = ? AND receiver_id = ?) OR (sender_id = ? AND receiver_id = ?)) "
            "ORDER BY id DESC LIMIT 1",
            senderId, receiverId, receiverId, senderId);
        if (!pendingResult.succeeded || !pendingResult.result)
        {
            db.rollbackTransaction();
            return FriendOperationStatus::Error;
        }
        try
        {
            if (pendingResult.result->next())
            {
                const auto existingSender = pendingResult.result->getUInt64("sender_id");
                pendingStatus = existingSender == senderId
                                    ? FriendOperationStatus::OutgoingRequestPending
                                    : FriendOperationStatus::IncomingRequestPending;
            }
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "检查重复好友申请失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            db.rollbackTransaction();
            return FriendOperationStatus::Error;
        }
    }
    if (pendingStatus != FriendOperationStatus::Success)
    {
        db.rollbackTransaction();
        return pendingStatus;
    }

    const int affectedRows = db.executeUpdate(
        "INSERT INTO FriendRequest (sender_id, receiver_id, status) VALUES (?, ?, 0)",
        senderId, receiverId);
    if (affectedRows != 1)
    {
        const int errorCode = db.lastErrorCode();
        db.rollbackTransaction();
        // 目前表结构没有 pending pair 的唯一键；这个分支仍然保留明确的数据库错误分类。
        if (errorCode == DUPLICATE_ENTRY_ERROR)
        {
            return FriendOperationStatus::OutgoingRequestPending;
        }
        return FriendOperationStatus::Error;
    }

    if (!readLastInsertId(db, requestId) || !db.commitTransaction())
    {
        db.rollbackTransaction();
        requestId = 0;
        return FriendOperationStatus::Error;
    }
    return FriendOperationStatus::Success;
}

FriendOperationStatus FriendRepository::getReceivedPendingRequests(
    std::uint64_t receiverId,
    std::vector<PendingFriendRequest> &requests)
{
    requests.clear();
    DatabaseManager db;
    if (!db.connect())
    {
        return FriendOperationStatus::Error;
    }
    // 先看获取人是否存在
    {
        auto activeUser = db.query(
            "SELECT 1 FROM User WHERE id = ? AND status = 1 AND deleted_at IS NULL LIMIT 1",
            receiverId);
        if (!activeUser.succeeded || !activeUser.result)
        {
            return FriendOperationStatus::Error;
        }
        try
        {
            if (!activeUser.result->next())
            {
                return FriendOperationStatus::UserUnavailable;
            }
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "验证好友申请列表的当前用户失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            return FriendOperationStatus::Error;
        }
    }

    auto result = db.query(
        "SELECT fr.id, fr.sender_id, u.username, u.nickname, COALESCE(u.avatar, '') AS avatar, fr.created_at "
        "FROM FriendRequest fr "
        "JOIN User u ON u.id = fr.sender_id "
        "WHERE fr.receiver_id = ? AND fr.status = 0 AND u.status = 1 AND u.deleted_at IS NULL "
        "ORDER BY fr.created_at ASC, fr.id ASC",
        receiverId);
    if (!result.succeeded || !result.result)
    {
        return FriendOperationStatus::Error;
    }

    try
    {
        while (result.result->next())
        {
            PendingFriendRequest item;
            item.requestId = result.result->getUInt64("id");
            item.senderId = result.result->getUInt64("sender_id");
            item.senderUsername = sqlStringToStdString(result.result->getString("username"));
            item.senderNickname = sqlStringToStdString(result.result->getString("nickname"));
            item.senderAvatar = sqlStringToStdString(result.result->getString("avatar"));
            item.createdAt = sqlStringToStdString(result.result->getString("created_at"));
            requests.push_back(std::move(item));
        }
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "读取收到的好友申请失败: " << e.what()
                  << "，错误代码: " << e.getErrorCode() << '\n';
        requests.clear();
        return FriendOperationStatus::Error;
    }
    return FriendOperationStatus::Success;
}

FriendOperationStatus FriendRepository::handleRequest(
    std::uint64_t receiverId,
    std::uint64_t requestId,
    FriendRequestAction action)
{
    if (receiverId == 0 || requestId == 0)
    {
        return FriendOperationStatus::RequestNotFound;
    }

    DatabaseManager db;
    if (!db.connect())
    {
        return FriendOperationStatus::Error;
    }
    if (!db.beginTransaction())
    {
        return FriendOperationStatus::Error;
    }

    std::uint64_t senderId = 0;
    std::uint64_t actualReceiverId = 0;
    std::uint32_t requestStatus = 0;
    {
        auto requestResult = db.query(
            "SELECT sender_id, receiver_id, status FROM FriendRequest WHERE id = ? FOR UPDATE",
            requestId);
        if (!requestResult.succeeded || !requestResult.result)
        {
            db.rollbackTransaction();
            return FriendOperationStatus::Error;
        }
        try
        {
            if (!requestResult.result->next())
            {
                db.rollbackTransaction();
                return FriendOperationStatus::RequestNotFound;
            }
            senderId = requestResult.result->getUInt64("sender_id");
            actualReceiverId = requestResult.result->getUInt64("receiver_id");
            requestStatus = requestResult.result->getUInt("status");
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "读取好友申请记录失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            db.rollbackTransaction();
            return FriendOperationStatus::Error;
        }
    }

    if (actualReceiverId != receiverId)
    {
        db.rollbackTransaction();
        return FriendOperationStatus::NotRequestReceiver;
    }
    if (requestStatus != 0)
    {
        db.rollbackTransaction();
        return FriendOperationStatus::RequestAlreadyHandled;
    }

    const int usersStatus = lockTwoActiveUsers(db, senderId, actualReceiverId);
    if (usersStatus != 1)
    {
        db.rollbackTransaction();
        return usersStatus == 0 ? FriendOperationStatus::UserUnavailable
                                : FriendOperationStatus::Error;
    }

    if (action == FriendRequestAction::Accept)
    {
        const auto low = std::min(senderId, actualReceiverId);
        const auto high = std::max(senderId, actualReceiverId);
        // 关系唯一键保证每对用户最多一行；如果之前是无效关系则重新激活。
        const int friendshipRows = db.executeUpdate(
            "INSERT INTO Friendship (user_id, friend_id, status) VALUES (?, ?, 1) "
            "ON DUPLICATE KEY UPDATE status = 1, updated_at = CURRENT_TIMESTAMP(3)",
            low, high);
        if (friendshipRows < 0)
        {
            db.rollbackTransaction();
            return FriendOperationStatus::Error;
        }
    }

    const int targetStatus = action == FriendRequestAction::Accept ? 1 : 2;
    const int updatedRows = db.executeUpdate(
        "UPDATE FriendRequest SET status = ? WHERE id = ? AND receiver_id = ? AND status = 0",
        targetStatus, requestId, receiverId);
    if (updatedRows != 1)
    {
        db.rollbackTransaction();
        return db.lastErrorCode() == 0 ? FriendOperationStatus::RequestAlreadyHandled
                                       : FriendOperationStatus::Error;
    }

    if (!db.commitTransaction())
    {
        db.rollbackTransaction();
        return FriendOperationStatus::Error;
    }
    return FriendOperationStatus::Success;
}

FriendOperationStatus FriendRepository::getFriends(
    std::uint64_t userId,
    std::vector<FriendInfo> &friends)
{
    friends.clear();
    DatabaseManager db;
    if (!db.connect())
    {
        return FriendOperationStatus::Error;
    }

    {
        auto activeUser = db.query(
            "SELECT 1 FROM User WHERE id = ? AND status = 1 AND deleted_at IS NULL LIMIT 1",
            userId);
        if (!activeUser.succeeded || !activeUser.result)
        {
            return FriendOperationStatus::Error;
        }
        try
        {
            if (!activeUser.result->next())
            {
                return FriendOperationStatus::UserUnavailable;
            }
        }
        catch (sql::SQLException &e)
        {
            std::cerr << "验证好友列表的当前用户失败: " << e.what()
                      << "，错误代码: " << e.getErrorCode() << '\n';
            return FriendOperationStatus::Error;
        }
    }

    auto result = db.query(
        "SELECT u.id AS friend_user_id, u.username, u.nickname, COALESCE(u.avatar, '') AS avatar, f.created_at "
        "FROM Friendship f "
        "JOIN User u ON u.id = CASE WHEN f.user_id = ? THEN f.friend_id ELSE f.user_id END "
        "WHERE (f.user_id = ? OR f.friend_id = ?) AND f.status = 1 "
        "AND u.status = 1 AND u.deleted_at IS NULL "
        "ORDER BY u.nickname ASC, u.id ASC",
        userId, userId, userId);
    if (!result.succeeded || !result.result)
    {
        return FriendOperationStatus::Error;
    }

    try
    {
        while (result.result->next())
        {
            FriendInfo item;
            item.userId = result.result->getUInt64("friend_user_id");
            item.username = sqlStringToStdString(result.result->getString("username"));
            item.nickname = sqlStringToStdString(result.result->getString("nickname"));
            item.avatar = sqlStringToStdString(result.result->getString("avatar"));
            item.since = sqlStringToStdString(result.result->getString("created_at"));
            friends.push_back(std::move(item));
        }
    }
    catch (sql::SQLException &e)
    {
        std::cerr << "读取好友列表失败: " << e.what()
                  << "，错误代码: " << e.getErrorCode() << '\n';
        friends.clear();
        return FriendOperationStatus::Error;
    }
    return FriendOperationStatus::Success;
}
