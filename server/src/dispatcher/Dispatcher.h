#pragma once 

#include "protocol/ProtocolHeader.h"
#include "service/UserService.h"
class Dispatcher
{
public:
    protocolHeader::PacketFrame dispatch(const protocolHeader::PacketFrame& frame);
};

