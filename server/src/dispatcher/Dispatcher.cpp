#include "Dispatcher.h"

protocolHeader::PacketFrame Dispatcher::dispatch(protocolHeader::PacketFrame frame)
{
    // 当前阶段先做最小的命令分发;
    // LOGIN_REQUEST -> LOGIN_FAILED
    // 其他命令      -> UNKNOWN
    std::uint16_t responseCommand = static_cast<std::uint16_t>(protocolHeader::CommandType::UNKNOWN);
    nlohmann::json responseBody;
    switch (static_cast<protocolHeader::CommandType>(frame.m_command))
    {
    case protocolHeader::CommandType::LOGIN_REQUEST:
    {
        responseCommand = static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_FAILED);
        responseBody =
            {
                {"code", 9999},
                {"message", "temporary test response: login failed"}};
        break;
    }
    default:
        responseBody =
            {
                {"code", 4001},
                {"message", "unsupported command"}};
        break;
    }
    protocolHeader::PacketFrame responseFrame(responseCommand,responseBody.dump(),frame.m_requestId);
    return responseFrame;
}