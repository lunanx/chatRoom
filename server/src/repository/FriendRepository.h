#pragma once

#include "db/DatabaseManager.h"
#include <cstdint>
#include <string>
#include <vector>

/*
 * FriendRepository 返回的业务结果。
 * Error 表示数据库或连接错误；其余状态可安全映射成面向客户端的提示。
 */
enum class FriendOperationStatus
{
    Success,
    UserNotFound,
    UserUnavailable,
    SelfRequest,
    AlreadyFriends,
    OutgoingRequestPending,
    IncomingRequestPending,
    RequestNotFound,
    NotRequestReceiver,
    RequestAlreadyHandled,
    Error
};

enum class FriendRequestAction
{
    Accept = 1,
    Reject = 2
};

struct PendingFriendRequest
{
    std::uint64_t requestId{0};
    std::uint64_t senderId{0};
    std::string senderUsername;
    std::string senderNickname;
    std::string senderAvatar;
    std::string createdAt;
};

struct FriendInfo
{
    std::uint64_t userId{0};
    std::string username;
    std::string nickname;
    std::string avatar;
    std::string since;
};

class FriendRepository
{
public:
    // targetUsername 是被申请者的登录用户名；senderId 必须来自已经认证的 ClientSession。
    static FriendOperationStatus sendRequest(
        std::uint64_t senderId,
        const std::string &targetUsername,
        std::uint64_t &requestId);

    // 只返回当前用户收到的待处理申请。
    static FriendOperationStatus getReceivedPendingRequests(
        std::uint64_t receiverId,
        std::vector<PendingFriendRequest> &requests);

    // 仅接收者可以处理申请；接受操作与好友关系写入在同一事务中完成。
    static FriendOperationStatus handleRequest(
        std::uint64_t receiverId,
        std::uint64_t requestId,
        FriendRequestAction action);

    // 返回当前有效好友，不返回无效好友关系或已经不可用的账号。
    static FriendOperationStatus getFriends(
        std::uint64_t userId,
        std::vector<FriendInfo> &friends);
};
