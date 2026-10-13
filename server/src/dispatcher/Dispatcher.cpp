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
        default:
            return protocolHeader::CommandType::UNKNOWN;
        }
    }

    std::string makeResponseBody(protocolHeader::CommandType command, const std::string &message)
    {
        const nlohmann::json response =
            {
                {protocolHeader::CODE, static_cast<std::uint16_t>(command)},
                {protocolHeader::MESSAGE, message}};
        return response.dump();
    }
}

protocolHeader::PacketFrame Dispatcher::dispatch(const protocolHeader::PacketFrame &frame, ClientSession &session)
{
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
        else if (command == protocolHeader::CommandType::GROUP_MESSAGE_SEND)
        {
            if (!session.isAuthenticated())
            {
                responseCommand = protocolHeader::CommandType::LOGOUT_FAILED;
                responseBody = makeResponseBody(
                    responseCommand,
                    "当前没有账号登录");
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

    return protocolHeader::PacketFrame(
        static_cast<std::uint16_t>(responseCommand),
        responseBody,
        frame.m_requestId);
}
