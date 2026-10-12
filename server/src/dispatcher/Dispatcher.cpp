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

protocolHeader::PacketFrame Dispatcher::dispatch(const protocolHeader::PacketFrame &frame)
{
    const auto command = static_cast<protocolHeader::CommandType>(frame.m_command);
    auto responseCommand = protocolHeader::CommandType::UNKNOWN;
    std::string responseBody;
    try
    {
        switch (command)
        {
        case protocolHeader::CommandType::REGISTER_REQUEST:
            responseCommand = UserService::registerUser(frame.m_body, responseBody);
            break;

        case protocolHeader::CommandType::LOGIN_REQUEST:
            responseCommand = UserService::loginUser(frame.m_body, responseBody);
            break;

        case protocolHeader::CommandType::LOGOUT_REQUEST:
            responseCommand = UserService::logoutUser(frame.m_body, responseBody);
            break;

        default:
        {
            // 未实现的命令也返回合法 JSON，避免客户端解析响应时遇到空白或非法内容。
            responseBody = makeResponseBody(
                protocolHeader::CommandType::UNKNOWN,
                "当前请求类型尚未实现");
            break;
        }
        }
    }
    catch (const std::exception &e)
    {
        // 具体异常只写入服务端日志，不将数据库/文件/库内部细节泄露给客户端。
        std::cerr << "请求处理异常: " << e.what() << '\n';
        responseCommand = failureCommandFor(command);
        responseBody = makeResponseBody(responseCommand, "服务器暂时无法处理请求，请稍后重试");
    }
    catch(...)
    {
        std::cerr<<"请求处理发生未知异常\n";
        responseCommand = failureCommandFor(command);
        responseBody = makeResponseBody(responseCommand,"服务器暂时无法处理请求，请稍后重试");
    }

    return protocolHeader::PacketFrame(
        static_cast<std::uint16_t>(responseCommand),
        responseBody,
        frame.m_requestId);
}
