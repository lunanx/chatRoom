#include "Dispatcher.h"
#include "protocol/ProtocolBody.h"

protocolHeader::PacketFrame Dispatcher::dispatch(const protocolHeader::PacketFrame& frame)
{
    const auto command = static_cast<protocolHeader::CommandType>(frame.m_command);
    auto responseCommand = protocolHeader::CommandType::UNKNOWN;
    std::string responseBody;

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
        const nlohmann::json response = {
            {protocolHeader::CODE, static_cast<std::uint16_t>(protocolHeader::CommandType::UNKNOWN)},
            {protocolHeader::MESSAGE, "当前请求类型尚未实现"}};
        responseBody = response.dump();
        break;
    }
    }

    return protocolHeader::PacketFrame(
        static_cast<std::uint16_t>(responseCommand),
        responseBody,
        frame.m_requestId);
}
