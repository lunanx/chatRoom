#pragma once

#include "protocol/ProtocolHeader.h"
#include <cstdint>
#include <string>

struct Packet
{
    protocolHeader::CommandType m_command;
    std::string m_body;
    std::uint64_t m_requestId;
};

class DecodedFrame
{
public:
    DecodedFrame(/* args */);
    ~DecodedFrame();
    int allocate(const struct Packet& bufPacket);
private:
};