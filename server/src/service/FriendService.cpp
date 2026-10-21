#include "FriendService.h"

#include "protocol/ProtocolBody.h"
#include "repository/FriendRepository.h"

#include <regex>
#include <utility>

namespace
{
    using protocolHeader::CommandType;

    CommandType setResponse(
        std::string &responseBody,
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

    bool isUnsignedInteger(const nlohmann::json &value)
    {
        if (value.is_number_unsigned())
        {
            return true;
        }
        return value.is_number_integer() && value.get<std::int64_t>() >= 0;
    }

    bool isValidUsername(const std::string &username)
    {
        if (username.size() < 3 || username.size() > 32)
        {
            return false;
        }
        static const std::regex pattern("^[A-Za-z0-9_!@#$%&*]+$");
        return std::regex_match(username, pattern);
    }
    // 好友请求
    std::string sendRequestFailureMessage(FriendOperationStatus status)
    {
        switch (status)
        {
        case FriendOperationStatus::UserNotFound:
            return "目标用户不存在或账号不可用";
        case FriendOperationStatus::UserUnavailable:
            return "当前用户或目标用户账号不可用";
        case FriendOperationStatus::SelfRequest:
            return "不能向自己发送好友申请";
        case FriendOperationStatus::AlreadyFriends:
            return "双方已经是好友，无需重复申请";
        case FriendOperationStatus::OutgoingRequestPending:
            return "好友申请已发送，请勿重复提交";
        case FriendOperationStatus::IncomingRequestPending:
            return "对方已向你发送好友申请，请在收到的申请中处理";
        case FriendOperationStatus::Error:
        default:
            return "服务器暂时无法处理好友申请，请稍后重试";
        }
    }
    //处理好友请求
    std::string handleRequestFailureMessage(FriendOperationStatus status)
    {
        switch (status)
        {
        case FriendOperationStatus::RequestNotFound:
            return "好友申请不存在";
        case FriendOperationStatus::NotRequestReceiver:
            return "只有好友申请接收者可以处理该申请";
        case FriendOperationStatus::RequestAlreadyHandled:
            return "该好友申请已被处理，不能重复操作";
        case FriendOperationStatus::UserUnavailable:
            return "申请双方至少有一个账号不可用";
        case FriendOperationStatus::Error:
        default:
            return "服务器暂时无法处理好友申请，请稍后重试";
        }
    }
    //用于查询谁发了
    nlohmann::json requestToJson(const PendingFriendRequest &request)
    {
        return {
            {protocolBody::FRIEND_REQUEST_ID, request.requestId},
            {protocolBody::USER_ID, request.senderId},
            {protocolBody::USERNAME, request.senderUsername},
            {protocolBody::NICKNAME, request.senderNickname},
            {protocolBody::AVATAR, request.senderAvatar},
            {protocolBody::CREATED_AT, request.createdAt}};
    }

    nlohmann::json friendToJson(const FriendInfo &friendInfo)
    {
        return {
            {protocolBody::USER_ID, friendInfo.userId},
            {protocolBody::USERNAME, friendInfo.username},
            {protocolBody::NICKNAME, friendInfo.nickname},
            {protocolBody::AVATAR, friendInfo.avatar},
            {protocolBody::CREATED_AT, friendInfo.since}};
    }
}

CommandType FriendService::sendFriendRequest(
    const std::string &body,
    std::string &responseBody,
    std::uint64_t authenticatedUserId)
{
    nlohmann::json data;
    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_SEND_FAILED,
                           "好友申请请求 JSON 格式不正确");
    }

    if (!data.is_object() ||
        !data.contains(protocolBody::TARGET_USERNAME) ||
        !data[protocolBody::TARGET_USERNAME].is_string())
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_SEND_FAILED,
                           "好友申请请求必须包含字符串 target_username");
    }

    const std::string targetUsername = data[protocolBody::TARGET_USERNAME].get<std::string>();
    if (!isValidUsername(targetUsername))
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_SEND_FAILED,
                           "目标用户名格式不正确");
    }
    if (authenticatedUserId == 0)
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_SEND_FAILED,
                           "当前登录用户无效，请重新登录");
    }

    std::uint64_t requestId = 0;
    const auto result = FriendRepository::sendRequest(
        authenticatedUserId, targetUsername, requestId);
    if (result != FriendOperationStatus::Success)
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_SEND_FAILED,
                           sendRequestFailureMessage(result));
    }

    return setResponse(responseBody, CommandType::FRIEND_REQUEST_SEND_SUCCESS,
                       "好友申请已发送",
                       nlohmann::json{{protocolBody::FRIEND_REQUEST_ID, requestId}});
}

CommandType FriendService::listFriendRequests(
    std::string &responseBody,
    std::uint64_t authenticatedUserId)
{
    if (authenticatedUserId == 0)
    {
        return setResponse(responseBody, CommandType::QUERY_PENDING_FRIEND_LIST_FAILED,
                           "当前登录用户无效，请重新登录");
    }

    std::vector<PendingFriendRequest> requests;
    const auto result = FriendRepository::getReceivedPendingRequests(authenticatedUserId, requests);
    if (result != FriendOperationStatus::Success)
    {
        const std::string message = result == FriendOperationStatus::UserUnavailable
                                        ? "当前用户账号不可用"
                                        : "服务器暂时无法查询好友申请，请稍后重试";
        return setResponse(responseBody, CommandType::QUERY_PENDING_FRIEND_LIST_FAILED, message);
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto &request : requests)
    {
        items.push_back(requestToJson(request));
    }
    return setResponse(responseBody, CommandType::QUERY_PENDING_FRIEND_LIST_SUCCESS,
                       "查询收到的好友申请成功", std::move(items));
}

CommandType FriendService::handleFriendRequest(
    const std::string &body,
    std::string &responseBody,
    std::uint64_t authenticatedUserId)
{
    nlohmann::json data;
    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_HANDLE_FAILED,
                           "处理好友申请的 JSON 格式不正确");
    }

    if (!data.is_object() ||
        !data.contains(protocolBody::FRIEND_REQUEST_ID) ||
        !isUnsignedInteger(data[protocolBody::FRIEND_REQUEST_ID]) ||
        !data.contains(protocolBody::ACTION) ||
        !isUnsignedInteger(data[protocolBody::ACTION]))
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_HANDLE_FAILED,
                           "请求必须包含正整数 friend_request_id 和 action");
    }

    const auto requestId = data[protocolBody::FRIEND_REQUEST_ID].get<std::uint64_t>();
    const auto actionValue = data[protocolBody::ACTION].get<std::uint64_t>();
    if (requestId == 0 || (actionValue != 1 && actionValue != 2))
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_HANDLE_FAILED,
                           "friend_request_id 必须大于 0，action 只能为 1（同意）或 2（拒绝）");
    }
    if (authenticatedUserId == 0)
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_HANDLE_FAILED,
                           "当前登录用户无效，请重新登录");
    }

    const auto action = actionValue == 1 ? FriendRequestAction::Accept : FriendRequestAction::Reject;
    const auto result = FriendRepository::handleRequest(authenticatedUserId, requestId, action);
    if (result != FriendOperationStatus::Success)
    {
        return setResponse(responseBody, CommandType::FRIEND_REQUEST_HANDLE_FAILED,
                           handleRequestFailureMessage(result));
    }

    return setResponse(responseBody, CommandType::FRIEND_REQUEST_HANDLE_SUCCESS,
                       action == FriendRequestAction::Accept ? "已同意好友申请" : "已拒绝好友申请",
                       nlohmann::json{{protocolBody::FRIEND_REQUEST_ID, requestId},
                                      {protocolBody::STATUS, action == FriendRequestAction::Accept ? 1 : 2}});
}

CommandType FriendService::listFriends(
    std::string &responseBody,
    std::uint64_t authenticatedUserId)
{
    if (authenticatedUserId == 0)
    {
        return setResponse(responseBody, CommandType::QUERY_FRIEND_LIST_FAILED,
                           "当前登录用户无效，请重新登录");
    }

    std::vector<FriendInfo> friends;
    const auto result = FriendRepository::getFriends(authenticatedUserId, friends);
    if (result != FriendOperationStatus::Success)
    {
        const std::string message = result == FriendOperationStatus::UserUnavailable
                                        ? "当前用户账号不可用"
                                        : "服务器暂时无法查询好友列表，请稍后重试";
        return setResponse(responseBody, CommandType::QUERY_FRIEND_LIST_FAILED, message);
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto &friendInfo : friends)
    {
        items.push_back(friendToJson(friendInfo));
    }
    return setResponse(responseBody, CommandType::QUERY_FRIEND_LIST_SUCCESS,
                       "查询好友列表成功", std::move(items));
}
