#include "Dispatcher.h"
#include "protocol/ProtocolBody.h"

#include <exception>
#include <iostream>

namespace
{
    protocolHeader::CommandType failureCommandFor(protocolHeader::CommandType requestCommand)
    {
        switch (requestCommand)
        {
        case protocolHeader::CommandType::REGISTER_REQUEST:
            return protocolHeader::CommandType::REGISTER_FAILED;
        case protocolHeader::CommandType::LOGIN_REQUEST:
            return protocolHeader::CommandType::LOGIN_FAILED;
        case protocolHeader::CommandType::LOGOUT_REQUEST:
            return protocolHeader::CommandType::LOGOUT_FAILED;
        case protocolHeader::CommandType::GROUP_MESSAGE_SEND:
        case protocolHeader::CommandType::PRIVATE_MESSAGE_SEND:
            return protocolHeader::CommandType::MESSAGE_SEND_FAILED;
        case protocolHeader::CommandType::CHATCREATE_REQUEST:
            return protocolHeader::CommandType::PRIVATECREATE_FAILED;
        case protocolHeader::CommandType::FRIEND_REQUEST_SEND:
            return protocolHeader::CommandType::FRIEND_REQUEST_SEND_FAILED;
        case protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_REQUEST:
            return protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_FAILED;
        case protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_REQUEST:
            return protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_FAILED;
        case protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST:
            return protocolHeader::CommandType::QUERY_FRIEND_LIST_FAILED;
        case protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST:
            return protocolHeader::CommandType::QUERY_CONVERSATION_LIST_FAILED;
        case protocolHeader::CommandType::GROUP_MEMBER_INVITE_REQUEST:
            return protocolHeader::CommandType::GROUP_MEMBER_INVITE_FAILED;
        default:
            return protocolHeader::CommandType::UNKNOWN;
        }
    }
    std::string makeResponseBody(protocolHeader::CommandType command, const std::string &message)
    {
        const nlohmann::json response =
            {
                {protocolBody::CODE, static_cast<std::uint16_t>(command)},
                {protocolBody::MESSAGE, message}};
        return response.dump();
    }

    protocolHeader::CommandType unauthenticatedConversationFailure(const std::string &body)
    {
        try
        {
            const auto data = nlohmann::json::parse(body);
            if (data.is_object() && data.contains(protocolBody::TYPE) &&
                data[protocolBody::TYPE].is_number_integer() &&
                data[protocolBody::TYPE].get<std::int64_t>() == 2)
            {
                return protocolHeader::CommandType::GROUPCREATE_FAILED;
            }
        }
        catch (...)
        {
        }
        return protocolHeader::CommandType::PRIVATECREATE_FAILED;
    }

}

DispatchResult Dispatcher::dispatch(const protocolHeader::PacketFrame &frame, ClientSession &session)
{
    DispatchResult dispatchResult;
    const auto command = static_cast<protocolHeader::CommandType>(frame.m_command);
    auto responseCommand = protocolHeader::CommandType::UNKNOWN;
    std::string responseBody;
    try
    {
        if (command == protocolHeader::CommandType::REGISTER_REQUEST)
        {
            responseCommand = UserService::registerUser(frame.m_body, responseBody);
        }
        else if (command == protocolHeader::CommandType::LOGIN_REQUEST)
        {
            if (session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::LOGIN_FAILED;
                responseBody = makeResponseBody(
                    responseCommand,
                    "当前已有账号登录，请先退出登录");
            }
            else
            {
                std::uint64_t userId = 0;
                std::string username = {};
                responseCommand = UserService::loginUser(
                    frame.m_body, responseBody, userId, username);
                if (responseCommand == protocolHeader::CommandType::LOGIN_SUCCESS)
                {
                    session.loginAs(static_cast<std::uint64_t>(userId), username);
                }
            }
        }
        else if (command == protocolHeader::CommandType::LOGOUT_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::LOGOUT_FAILED;
                responseBody = makeResponseBody(
                    responseCommand,
                    "当前没有账号登录");
            }
            else
            {
                std::optional<AuthenticatedUser> user = session.getAuthenticatedUser();
                responseCommand = UserService::logoutUser(user->username, responseBody);
                if (responseCommand == protocolHeader::CommandType::LOGOUT_SUCCESS)
                {
                    session.logout();
                }
            }
        }
        else if (command == protocolHeader::CommandType::GROUP_MESSAGE_SEND ||
                 command == protocolHeader::CommandType::PRIVATE_MESSAGE_SEND)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::MESSAGE_SEND_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                const MessageSendResult result = MessageService::sendContent(
                    frame.m_body, responseBody, user->userId, user->username);
                responseCommand = result.command;
                if (result.shouldBroadcast && result.message.has_value())
                {
                    dispatchResult.message = result.message;
                }
            }
        }
        else if (command == protocolHeader::CommandType::CHATCREATE_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = unauthenticatedConversationFailure(frame.m_body);
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                const ConversationCreateResult result = ConversationService::createConversation(
                    frame.m_body, responseBody, user->userId);
                responseCommand = result.command;
                dispatchResult.conversationId = result.conversationId;
                dispatchResult.lastSeq = result.lastSeq;
            }
        }
        else if (command == protocolHeader::CommandType::MESSAGE_HISTORY_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::MESSAGE_HISTORY_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                responseCommand = MessageService::MessageHistoryQuery(frame.m_body, responseBody, user->userId);
            }
        }
        // 邀请好友加入群聊。邀请人身份只取自服务端认证会话。
        else if (command == protocolHeader::CommandType::GROUP_MEMBER_INVITE_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::GROUP_MEMBER_INVITE_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                const GroupInviteResult result = GroupInviteService::inviteMembers(
                    frame.m_body, responseBody, user->userId);
                responseCommand = result.command;
                if (responseCommand == protocolHeader::CommandType::GROUP_MEMBER_INVITE_SUCCESS)
                {
                    // 记录邀请人对群会话的订阅关系，并在 EpollReactor 中通知新增成员。
                    dispatchResult.conversationId = result.conversationId;
                    dispatchResult.invitedUserIds = result.invitedUserIds;
                }
            }
        }
        // 查询会话列表
        else if (command == protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::QUERY_CONVERSATION_LIST_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                responseCommand = ConversationService::listConversations(responseBody, user->userId);
            }
        }
        // 好友请求--A->B
        else if (command == protocolHeader::CommandType::FRIEND_REQUEST_SEND)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::FRIEND_REQUEST_SEND_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                responseCommand = FriendService::sendFriendRequest(
                    frame.m_body, responseBody, user->userId);
            }
        }
        // 查询好友请求列表--B->A
        else if (command == protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                responseCommand = FriendService::listFriendRequests(responseBody, user->userId);
            }
        }
        // 处理好友请求--B->A
        else if (command == protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                responseCommand = FriendService::handleFriendRequest(
                    frame.m_body, responseBody, user->userId);
            }
        }
        // 查询好友列表
        else if (command == protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::QUERY_FRIEND_LIST_FAILED;
                responseBody = makeResponseBody(responseCommand, "当前没有账号登录");
            }
            else
            {
                const auto &user = session.getAuthenticatedUser();
                responseCommand = FriendService::listFriends(responseBody, user->userId);
            }
        }
        else
        {
            // 未实现的命令也返回合法 JSON，避免客户端解析响应时遇到空白或非法内容。
            responseBody = makeResponseBody(
                protocolHeader::CommandType::UNKNOWN,
                "当前请求类型尚未实现");
        }
    }
    catch (const std::exception &e)
    {
        // 具体异常只写入服务端日志，不将数据库/文件/库内部细节泄露给客户端。
        std::cerr << "请求处理异常: " << e.what() << '\n';
        responseCommand = failureCommandFor(command);
        responseBody = makeResponseBody(responseCommand, "服务器暂时无法处理请求，请稍后重试");
    }
    catch (...)
    {
        std::cerr << "请求处理发生未知异常\n";
        responseCommand = failureCommandFor(command);
        responseBody = makeResponseBody(responseCommand, "服务器暂时无法处理请求，请稍后重试");
    }

    dispatchResult.frame = protocolHeader::PacketFrame(static_cast<std::uint16_t>(responseCommand), responseBody, frame.m_requestId);
    return dispatchResult;
}
