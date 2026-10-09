#include "Dispatcher.h"

protocolHeader::PacketFrame Dispatcher::dispatch(const protocolHeader::PacketFrame& frame)
{

    protocolHeader::CommandType responseCommand = protocolHeader::CommandType::UNKNOWN;
    std::string responseBody = "{}";
    switch (static_cast<protocolHeader::CommandType>(frame.m_command))
    {
    case protocolHeader::CommandType::REGISTER_REQUEST:
    {
        responseCommand = UserService::registerUser(frame.m_body,responseBody);
        break;
    }
    default:
        
        break;
    }

    return protocolHeader::PacketFrame(static_cast<std::uint16_t>(responseCommand),responseBody,frame.m_requestId);
}